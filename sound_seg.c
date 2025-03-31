#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// 修改结构体：将 next 指针类型改为 seg_node*
typedef struct seg_node {
    int16_t* samples;      // 指向采样数据（非共享节点中指向自己分配的内存；共享节点中通常为 NULL）
    size_t length;         // 本节点的样本数量
    bool shared;           // 是否为共享节点（true 表示数据不在本节点，而引用父段数据）
    struct sound_seg* parent; // 如果共享，则指向父 track（父 track 存储着真正数据）
    struct seg_node* next; // 指向下一个节点
    size_t parent_offset;  // 如果共享，则在父 track 中的起始偏移
} seg_node;

typedef struct sound_seg {
    seg_node* head; // 链表头
    size_t length;  // 整个 track 的样本总数（所有节点长度之和）
} sound_seg;

/* --- 以下函数不作修改 --- */

double cross_correlation(const int16_t* a, const int16_t* b, size_t len);
double auto_correlation(const int16_t* a, size_t len);

// Load a WAV file into buffer
void wav_load(const char* filename, int16_t* dest){
    FILE* f = fopen(filename, "rb");
    if (!f) return;
    fseek(f, 44, SEEK_SET);
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    long dataSize = fileSize - 44;
    fseek(f, 44, SEEK_SET);
    fread(dest, 1, dataSize, f);
    fclose(f);
    return;
}

// Create/write a WAV file from buffer
void wav_save(const char* fname, int16_t* src, size_t len){
    FILE *f = fopen(fname, "wb");
    if (!f) return;
    uint32_t dataSize = (uint32_t)(len * sizeof(int16_t));
    uint32_t chunkSize = 36 + dataSize;
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunkSize, sizeof(uint32_t), 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    uint32_t subchunk1Size = 16;
    fwrite(&subchunk1Size, sizeof(uint32_t), 1, f);
    uint16_t audioFormat = 1;
    uint16_t numChannels = 1;
    uint16_t blockAlign = numChannels * 2;
    uint16_t bitsPerSample = 16;
    uint32_t sampleRate = 8000;
    uint32_t byteRate = sampleRate * numChannels * 2;
    fwrite(&audioFormat, sizeof(uint16_t), 1, f);
    fwrite(&numChannels, sizeof(uint16_t), 1, f);
    fwrite(&sampleRate, sizeof(uint32_t), 1, f);
    fwrite(&byteRate, sizeof(uint32_t), 1, f);
    fwrite(&blockAlign, sizeof(uint16_t), 1, f);
    fwrite(&bitsPerSample, sizeof(uint16_t), 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, sizeof(uint32_t), 1, f);
    fwrite(src, sizeof(int16_t), len, f);
    fclose(f);
    return;
}

/* --- Track 相关函数 --- */

// 初始化 track
sound_seg* tr_init() {
    sound_seg* track = malloc(sizeof(sound_seg));
    if (!track) return NULL;
    track->head = NULL;
    track->length = 0;
    return track;
}

// 销毁 track，遍历链表释放所有节点及其数据
void tr_destroy(sound_seg* track) {
    if (!track) return;
    seg_node* curr = track->head;
    while (curr) {
        seg_node* next = curr->next;
        // 仅释放非共享节点的数据（共享节点的数据由父 track 管理）
        if (curr->samples && (!curr->shared || !curr->parent)) {
            free(curr->samples);
        }
        free(curr);
        curr = next;
    }
    free(track);
    return;
}

// 返回 track 的总长度
size_t tr_length(sound_seg* seg) {
    if (!seg) return 0;
    return seg->length;
}

/* --- 数据读写函数 --- */

// 递归实现 tr_read：如果遇到共享节点，则直接调用父 track 的 tr_read
void tr_read(sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest) return;
    if (pos >= track->length) return;
    if (pos + len > track->length) len = track->length - pos;

    size_t totalRead = 0;
    size_t segStart = 0;
    seg_node* curr = track->head;
    while (curr && totalRead < len) {
        size_t segEnd = segStart + curr->length;
        if (pos < segEnd) {
            size_t offsetInNode = (pos > segStart) ? pos - segStart : 0;
            size_t available = curr->length - offsetInNode;
            size_t toRead = (len - totalRead < available) ? (len - totalRead) : available;
            if (curr->shared && curr->parent) {
                // 直接递归调用父 track 的 tr_read
                tr_read(curr->parent, dest + totalRead, curr->parent_offset + offsetInNode, toRead);
            } else {
                memcpy(dest + totalRead, curr->samples + offsetInNode, toRead * sizeof(int16_t));
            }
            totalRead += toRead;
            pos += toRead;
        }
        segStart = segEnd;
        curr = curr->next;
    }
    return;
}

// 递归实现 tr_write：遇到共享节点时，调用父 track 的 tr_write
void tr_write(sound_seg* track, int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    if (pos > track->length) pos = track->length;

    size_t totalWritten = 0;
    size_t segStart = 0;
    seg_node* curr = track->head;

    // 若 pos 等于 track->length 或链表为空，直接创建新节点追加
    if (pos == track->length || !curr) {
        seg_node* newNode = malloc(sizeof(seg_node));
        if (!newNode) return;
        newNode->samples = malloc(len * sizeof(int16_t));
        if (!newNode->samples) {
            free(newNode);
            return;
        }
        memcpy(newNode->samples, src, len * sizeof(int16_t));
        newNode->length = len;
        newNode->shared = false;
        newNode->parent = NULL;
        newNode->next = NULL;
        newNode->parent_offset = 0;
        if (!track->head) {
            track->head = newNode;
        } else {
            seg_node* cur = track->head;
            while (cur->next) {
                cur = cur->next;
            }
            cur->next = newNode;
        }
        track->length += len;
        return;
    }

    // 遍历链表，将数据写入相应节点
    while (curr && totalWritten < len) {
        size_t segEnd = segStart + curr->length;
        if (pos < segEnd) {
            size_t offsetInNode = (pos > segStart) ? pos - segStart : 0;
            size_t available = curr->length - offsetInNode;
            size_t toWrite = (len - totalWritten < available) ? (len - totalWritten) : available;
            if (curr->shared && curr->parent) {
                // 直接递归调用父 track 的 tr_write
                tr_write(curr->parent, src + totalWritten, curr->parent_offset + offsetInNode, toWrite);
            } else {
                memcpy(curr->samples + offsetInNode, src + totalWritten, toWrite * sizeof(int16_t));
            }
            totalWritten += toWrite;
            pos += toWrite;
        }
        segStart = segEnd;
        curr = curr->next;
    }

    // 如果还有未写入数据，则创建新节点追加
    if (totalWritten < len) {
        size_t remaining = len - totalWritten;
        seg_node* new_node = malloc(sizeof(seg_node));
        if (!new_node) return;
        new_node->samples = malloc(remaining * sizeof(int16_t));
        if (!new_node->samples) {
            free(new_node);
            return;
        }
        memcpy(new_node->samples, src + totalWritten, remaining * sizeof(int16_t));
        new_node->length = remaining;
        new_node->shared = false;
        new_node->parent = NULL;
        new_node->next = NULL;
        new_node->parent_offset = 0;
        seg_node* current = track->head;
        while (current->next) {
            current = current->next;
        }
        current->next = new_node;
        track->length += remaining;
    }
    return;
}

/* --- 旧版 delete 采用连续数组方案 --- */

// 删除范围内的数据（旧数组方式实现）
bool tr_delete_range(sound_seg* track, size_t pos, size_t len) {
    if (!track || !track->head || !track->head->samples) return false;
    if (pos >= track->length) return false;
    if (pos + len > track->length) {
        len = track->length - pos;
    }
    size_t remain = track->length - pos - len;
    memmove(track->head->samples + pos,
            track->head->samples + pos + len,
            remain * sizeof(int16_t));
    track->length -= len;
    track->head->length = track->length;
    return true;
}

/* --- 识别函数 --- */

// 返回一个字符串，包含匹配的 <start>,<end> 对
char* tr_identify(const sound_seg* target, const sound_seg* ad) {
    if (!target || !ad) {
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t tlen = tr_length((sound_seg*)target);
    size_t alen = tr_length((sound_seg*)ad);
    if (tlen == 0 || alen == 0 || alen > tlen) {
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    int16_t* target_data = malloc(tlen * sizeof(int16_t));
    int16_t* ad_data = malloc(alen * sizeof(int16_t));
    if (!target_data || !ad_data) {
        free(target_data);
        free(ad_data);
        char* empty = malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    tr_read((sound_seg*)target, target_data, 0, tlen);
    tr_read((sound_seg*)ad, ad_data, 0, alen);
    
    double reference = auto_correlation(ad_data, alen);
    double threshold = reference * 0.95;

    size_t initial_size = 256;
    char* result = malloc(initial_size);
    if (!result) {
        free(target_data);
        free(ad_data);
        return NULL;
    }
    result[0] = '\0';
    size_t resultPos = 0;
    size_t matchCount = 0;
    size_t offset = 0;
    size_t last_matched_end = 0;
    size_t result_size = initial_size;
    while (offset + alen <= tlen) {
        if (offset <= last_matched_end) {
            offset++;
            continue;
        }
        double cc = cross_correlation(target_data + offset, ad_data, alen);
        if (cc >= threshold) {
            size_t start = offset;
            size_t end = offset + alen - 1;
            last_matched_end = end;
            if (matchCount > 0) {
                if (resultPos + 1 >= result_size) {
                    size_t new_size = result_size * 2;
                    char* new_result = realloc(result, new_size);
                    if (!new_result) break;
                    result = new_result;
                    result_size = new_size;
                }
                result[resultPos++] = '\n';
                result[resultPos] = '\0';
            }
            char pair[64];
            int written = snprintf(pair, sizeof(pair), "%zu,%zu", start, end);
            if (written < 0 || written >= (int)sizeof(pair)) {
                offset++;
                break;
            }
            if (resultPos + written >= result_size) {
                size_t new_size = result_size * 2;
                while (new_size <= resultPos + written) {
                    new_size *= 2;
                }
                char* new_result = realloc(result, new_size);
                if (!new_result) break;
                result = new_result;
                result_size = new_size;
            }
            strcpy(result + resultPos, pair);
            resultPos += written;
            matchCount++;
            offset = end + 1;
        } else {
            offset++;
        }
    }
    free(target_data);
    free(ad_data);
    return result;
}

/* --- Insert 函数 --- */

// 在 dest_track 的 destpos 位置插入 src_track 中从 srcpos 开始长度为 len 的数据
void tr_insert(sound_seg* src_track,
               sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0) return;
    if (destpos > dest_track->length) destpos = dest_track->length;
    if (srcpos >= src_track->length) return;
    if (srcpos + len > src_track->length) len = src_track->length - srcpos;
    if (len == 0) return;

    seg_node* curr = dest_track->head;
    seg_node* prev = NULL;
    size_t segStart = 0;
    while (curr) {
        size_t segEnd = segStart + curr->length;
        if (destpos < segEnd) break;
        segStart = segEnd;
        prev = curr;
        curr = curr->next;
    }
    if (curr) {
        size_t offsetInNode = destpos - segStart;
        if (offsetInNode > 0 && offsetInNode < curr->length) {
            seg_node* tail_node = malloc(sizeof(seg_node));
            if (!tail_node) return;
            tail_node->length = curr->length - offsetInNode;
            tail_node->shared = curr->shared;
            tail_node->parent = curr->parent;
            tail_node->parent_offset = curr->parent_offset + offsetInNode;
            tail_node->next = curr->next;
            tail_node->samples = (curr->samples) ? (curr->samples + offsetInNode) : NULL;
            curr->length = offsetInNode;
            curr->next = tail_node;
        }
    }
    seg_node* shared_node = malloc(sizeof(seg_node));
    if (!shared_node) return;
    shared_node->length = len;
    shared_node->shared = true;
    shared_node->parent = src_track;
    shared_node->parent_offset = srcpos;
    shared_node->next = NULL;
    shared_node->samples = NULL;
    if (!dest_track->head) {
        dest_track->head = shared_node;
    } else if (!curr) {
        if (prev) {
            prev->next = shared_node;
        } else {
            dest_track->head = shared_node;
        }
    } else {
        shared_node->next = curr->next;
        curr->next = shared_node;
    }
    dest_track->length += len;
}
 
// 计算相关性（逐元素乘积求和）
double cross_correlation(const int16_t* a, const int16_t* b, size_t len) {
    double corr = 0.0;
    for (size_t i = 0; i < len; i++) {
        corr += (double)a[i] * (double)b[i];
    }
    return corr;
}

// 自相关
double auto_correlation(const int16_t* a, size_t len) {
    return cross_correlation(a, a, len);
}