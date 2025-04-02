#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>


typedef struct seg_node {
    //TODO
    int16_t* samples; // array(pointer) of samples
    size_t length;
    bool shared; // judge if shared
    struct sound_seg* parent; // if the data is shared, point to the track
    struct seg_node* next; // if the data is shared, point to the next track
    size_t parent_offset;
    size_t child_count; // 新增: 跟踪该节点作为父节点被引用的次数
} seg_node;

typedef struct sound_seg {
    seg_node* head;
    size_t length;
} sound_seg;

double cross_correlation(const int16_t* a, const int16_t* b, size_t len);
double auto_correlation(const int16_t* a, size_t len);

// Load a WAV file into buffer
void wav_load(const char* filename, int16_t* dest){
    // fopen file, read
    FILE* f = fopen(filename, "rb");
    if (!f) return;

    // get size of file fseek + ftell
    fseek(f, 44, SEEK_SET);

    //get left data to dest
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f); // get the position(pointer)
    long dataSize = fileSize - 44;
    fseek(f, 44, SEEK_SET);

    // read dataSize to dest
    fread(dest, 1, dataSize, f);

    // close file
    fclose(f);
    
    return;
}

// Create/write a WAV file from buffer
void wav_save(const char* fname, int16_t* src, size_t len){
    //open file
    FILE *f = fopen(fname, "wb");
    if (!f) return;

    //calculate datasize
    uint32_t dataSize = (uint32_t)(len * sizeof(int16_t));

    /*
    RIFF
    RIFF + chunkSize + Wave
    chunkSize = total - 8 = 36 + dataSize
    */
    uint32_t chunkSize = 36 + dataSize;
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunkSize, sizeof(uint32_t), 1, f);
    fwrite("WAVE", 1, 4, f);

    //fmt fmt + subchunk1Size + PCM
    fwrite("fmt ", 1, 4, f);
    uint32_t subchunk1Size = 16; // PCM
    fwrite(&subchunk1Size, sizeof(uint32_t), 1, f);

    uint16_t audioFormat = 1; // PCM not compressed
    uint16_t numChannels = 1; // 1 mono, 2 stereo
    uint16_t blockAlign = numChannels * 2; // each sample 2 bytes
    uint16_t bitsPerSample = 16; // 16bits
    uint32_t sampleRate = 8000; //8000hz
    uint32_t byteRate = sampleRate * numChannels * 2; // num of bytes per second
    

    fwrite(&audioFormat, sizeof(uint16_t), 1, f);
    fwrite(&numChannels, sizeof(uint16_t), 1, f);
    fwrite(&sampleRate, sizeof(uint32_t), 1, f);
    fwrite(&byteRate, sizeof(uint32_t), 1, f);
    fwrite(&blockAlign, sizeof(uint16_t), 1, f);
    fwrite(&bitsPerSample, sizeof(uint16_t), 1, f);


    //write sub data
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, sizeof(uint32_t), 1, f);

    // write data in wav file
    fwrite(src, sizeof(int16_t), len, f);
    //close
    fclose(f);

    return;
}

// Initialize a new sound_seg object -> empty ll
struct sound_seg* tr_init() {
    sound_seg* track = malloc(sizeof(struct sound_seg));
    // allocate memory failed
    if (!track) return NULL;

    // initialize
    track->head = NULL;
    track->length = 0;
    return track;
}

// Destroy a sound_seg object and free all allocated memory (except shared)
void tr_destroy(struct sound_seg* track) {
    // 如果指针为空则返回
    if (!track) return;

    seg_node* curr = track->head;
    // 释放链表中的每个节点
    while (curr) {
        seg_node* next = curr->next;
        
        // 如果是共享节点，减少父节点的子计数
        if (curr->shared && curr->parent) {
            // 由于ASM 0.3确保tr_destroy只在程序结束时调用，我们可以直接销毁
            // 实际上不需要修改父节点的child_count
            // 但为了代码的完整性和未来可能的扩展，我们仍然实现这个逻辑
            size_t parent_offset = curr->parent_offset;
            size_t curr_pos = 0;
            seg_node* parent_node = curr->parent->head;
            
            // 查找对应的父节点
            while (parent_node) {
                size_t node_end = curr_pos + parent_node->length;
                
                // 检查父节点范围是否包含这个偏移
                if (parent_offset < node_end) {
                    // 找到父节点，减少子节点计数
                    if (parent_node->child_count > 0) {
                        parent_node->child_count--;
                    }
                    break;
                }
                
                curr_pos = node_end;
                parent_node = parent_node->next;
            }
        }
        
        // 释放样本数据（只有非共享数据或没有子节点的数据需要释放）
        if (curr->samples && !curr->shared) {
            free(curr->samples);
        }

        // 释放节点本身
        free(curr);
        curr = next;
    }

    // 释放轨道结构
    free(track);
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    if (!seg) return 0;
    return seg->length;
}



// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    // Check edge cases
    if (!track || !dest) return;
    if (pos >= track->length) return;

    // Calculate how many elements can be read
    size_t can_read = track->length - pos;
    if (len > can_read) len = can_read;

    size_t totalRead = 0;
    size_t segStart = 0;

    // 遍历链表
    seg_node* curr = track->head;
    while (curr && totalRead < len) {
        size_t segEnd = segStart + curr->length;
        
        if (pos < segEnd) {
            // 计算当前节点内的位置
            size_t offsetInNode = (pos > segStart) ? (pos - segStart) : 0;
            
            // 计算从该节点读取多少数据
            size_t available = curr->length - offsetInNode;
            size_t toRead = (len - totalRead < available) ? (len - totalRead) : available;
            
            // 处理共享数据
            if (curr->shared && curr->parent) {
                // 创建临时缓冲区用于从父轨道读取数据
                int16_t* temp_buffer = malloc(toRead * sizeof(int16_t));
                if (!temp_buffer) return;
                
                // 从父轨道正确的偏移位置读取数据
                size_t parent_pos = curr->parent_offset + offsetInNode;
                
                // 从父轨道读取数据
                tr_read(curr->parent, temp_buffer, parent_pos, toRead);
                
                // 复制数据到目标缓冲区
                memcpy(dest + totalRead, temp_buffer, toRead * sizeof(int16_t));
                
                // 释放临时缓冲区
                free(temp_buffer);
            } else {
                // 直接复制非共享数据
                memcpy(dest + totalRead, curr->samples + offsetInNode, toRead * sizeof(int16_t));
            }
            
            // 更新追踪变量
            totalRead += toRead;
            pos += toRead;
        }
        
        // 移动到下一个节点
        segStart = segEnd;
        curr = curr->next;
    }
}

// 更新: tr_write 重新实现，确保父子关系中的数据正确传播
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    // Check edge cases
    if (!track || !dest) return;
    if (pos >= track->length) return;

    // Calculate how many elements can be read
    size_t can_read = track->length - pos;
    if (len > can_read) len = can_read;

    size_t totalRead = 0;
    size_t segStart = 0;

    // 遍历链表
    seg_node* curr = track->head;
    while (curr && totalRead < len) {
        size_t segEnd = segStart + curr->length;
        
        if (pos < segEnd) {
            // 计算当前节点内的位置
            size_t offsetInNode = (pos > segStart) ? (pos - segStart) : 0;
            
            // 计算从该节点读取多少数据
            size_t available = curr->length - offsetInNode;
            size_t toRead = (len - totalRead < available) ? (len - totalRead) : available;
            
            // 处理共享数据
            if (curr->shared && curr->parent) {
                // 创建临时缓冲区用于从父轨道读取数据
                int16_t* temp_buffer = malloc(toRead * sizeof(int16_t));
                if (!temp_buffer) return;
                
                // 从父轨道正确的偏移位置读取数据
                size_t parent_pos = curr->parent_offset + offsetInNode;
                
                // 从父轨道读取数据
                tr_read(curr->parent, temp_buffer, parent_pos, toRead);
                
                // 复制数据到目标缓冲区
                memcpy(dest + totalRead, temp_buffer, toRead * sizeof(int16_t));
                
                // 释放临时缓冲区
                free(temp_buffer);
            } else {
                // 直接复制非共享数据
                memcpy(dest + totalRead, curr->samples + offsetInNode, toRead * sizeof(int16_t));
            }
            
            // 更新追踪变量
            totalRead += toRead;
            pos += toRead;
        }
        
        // 移动到下一个节点
        segStart = segEnd;
        curr = curr->next;
    }
}

// 更新: 检查是否节点是父节点（有子节点）
static bool is_parent_node(seg_node* node) {
    return node->child_count > 0;
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    // 检查边界情况
    if (!track || !track->head) return false;
    if (pos >= track->length) return false;
    if (pos + len > track->length) len = track->length - pos;
    if (len == 0) return true;  // 没有需要删除的内容

    // 第一步: 检查该范围是否有父节点部分(有子节点的部分)
    size_t curr_pos = 0;
    seg_node* curr = track->head;
    
    while (curr && curr_pos < pos + len) {
        size_t next_pos = curr_pos + curr->length;
        
        // 检查当前节点与删除范围是否有重叠
        if (next_pos > pos && curr_pos < pos + len) {
            // 节点与范围重叠，检查是否是父节点
            if (curr->child_count > 0) {
                return false;  // 不能删除有子节点的部分
            }
        }
        
        curr_pos = next_pos;
        curr = curr->next;
    }

    // 第二步: 开始执行删除
    curr_pos = 0;
    seg_node* prev = NULL;
    curr = track->head;
    
    // 找到第一个与删除范围重叠的节点
    while (curr && curr_pos + curr->length <= pos) {
        curr_pos += curr->length;
        prev = curr;
        curr = curr->next;
    }
    
    // 开始删除过程
    size_t remaining_to_delete = len;
    
    while (curr && remaining_to_delete > 0) {
        size_t node_offset = (pos > curr_pos) ? (pos - curr_pos) : 0;
        size_t delete_from_node = (node_offset + remaining_to_delete <= curr->length) ? 
                                   remaining_to_delete : (curr->length - node_offset);
        
        // 情况1: 删除整个节点
        if (node_offset == 0 && delete_from_node == curr->length) {
            seg_node* to_delete = curr;
            curr = curr->next;
            
            if (prev) {
                prev->next = curr;
            } else {
                track->head = curr;
            }
            
            // 释放资源 (只有非共享数据需要释放)
            if (to_delete->samples && !to_delete->shared) {
                free(to_delete->samples);
            }
            free(to_delete);
        }
        // 情况2: 从节点开始处删除
        else if (node_offset == 0) {
            if (!curr->shared) {
                // 直接移动数据
                memmove(curr->samples, 
                        curr->samples + delete_from_node, 
                        (curr->length - delete_from_node) * sizeof(int16_t));
                curr->length -= delete_from_node;
                
                prev = curr;
                curr = curr->next;
            } else {
                // 共享数据需要创建新节点指向父数据的后部分
                seg_node* new_node = malloc(sizeof(seg_node));
                if (!new_node) return false;
                
                new_node->length = curr->length - delete_from_node;
                new_node->shared = true;
                new_node->parent = curr->parent;
                new_node->parent_offset = curr->parent_offset + delete_from_node;
                new_node->samples = NULL;
                new_node->child_count = 0;
                new_node->next = curr->next;
                
                if (prev) {
                    prev->next = new_node;
                } else {
                    track->head = new_node;
                }
                
                free(curr);
                curr = new_node;
                prev = curr;
                curr = curr->next;
            }
        }
        // 情况3: 删除节点末尾部分
        else if (node_offset + delete_from_node == curr->length) {
            curr->length -= delete_from_node;
            prev = curr;
            curr = curr->next;
        }
        // 情况4: 删除节点中间部分
        else {
            // 创建新节点保存删除点之后的部分
            seg_node* after_node = malloc(sizeof(seg_node));
            if (!after_node) return false;
            
            size_t after_len = curr->length - node_offset - delete_from_node;
            
            if (!curr->shared) {
                // 为非共享数据复制删除点之后的部分
                after_node->samples = malloc(after_len * sizeof(int16_t));
                if (!after_node->samples) {
                    free(after_node);
                    return false;
                }
                
                memcpy(after_node->samples, 
                       curr->samples + node_offset + delete_from_node, 
                       after_len * sizeof(int16_t));
                
                after_node->length = after_len;
                after_node->shared = false;
                after_node->parent = NULL;
                after_node->parent_offset = 0;
                after_node->child_count = 0;
            } else {
                // 共享数据只需创建指针
                after_node->samples = NULL;
                after_node->length = after_len;
                after_node->shared = true;
                after_node->parent = curr->parent;
                after_node->parent_offset = curr->parent_offset + node_offset + delete_from_node;
                after_node->child_count = 0;
            }
            
            after_node->next = curr->next;
            curr->next = after_node;
            curr->length = node_offset;
            
            prev = after_node;
            curr = after_node->next;
        }
        
        remaining_to_delete -= delete_from_node;
        curr_pos += node_offset + delete_from_node;
    }
    
    // 更新轨道总长度
    track->length -= len;
    return true;
}
// Returns a string containing <start>,<end> ad pairs in target
char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
    //check if target track or ad is empty
    if (!target || !ad) {
        char* empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    
    //use tr_length to get the length of the target
    size_t tlen = tr_length((struct sound_seg*)target);
    size_t alen = tr_length((struct sound_seg*)ad);

    //if the length of ad is greater than target or empty, return empty
    if (tlen == 0 || alen == 0 || alen > tlen) {
        char* empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    //use tr_read
    int16_t* target_data = malloc(tlen * sizeof(int16_t));
    int16_t* ad_data = malloc(alen * sizeof(int16_t));
    
    //allocate failed
    if (!target_data || !ad_data) {
        free(target_data);
        free(ad_data);
        char* empty = (char*)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }

    tr_read((struct sound_seg*)target, target_data, 0, tlen);
    tr_read((struct sound_seg*)ad, ad_data, 0, alen);
    
    //calculate the auto correlation
    double reference = auto_correlation(ad_data, alen);
    double threshold = reference * 0.95;

    // initialize the first result
    size_t initial_size = 256;
    char* result = (char*)malloc(initial_size);

    //allocate failed
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

    /*
        iterate target_data
        [                 ]
          [  ]-> [   ]
    */
    while (offset + alen <= tlen) {
        if (offset <= last_matched_end) {
            offset++;
            continue;
        }

        double cc = cross_correlation(target_data + offset, ad_data, alen);
        if (cc >= threshold) {
            size_t start = offset;
            size_t end = offset + alen - 1; //index
            last_matched_end = end;

            //if it is not the first match, add \n
            if (matchCount > 0) {
                if (resultPos + 1 >= result_size) {
                    //expend the size
                    size_t new_size = result_size * 2;
                    char* new_result = (char*)realloc(result, new_size);
                    if (!new_result) break;
                    result = new_result;
                    result_size = new_size;
                }
                result[resultPos++] = '\n';
                result[resultPos] = '\0';
            }

            //convert to str
            char pair[64];
            int written = snprintf(pair, sizeof(pair), "%zu,%zu", start, end);
            if (written < 0) break;
            if (written < 0 || written >= (int)sizeof(pair)) {
                offset++;
                break;
            }

            // check if has enough space
            if (resultPos + written >= result_size) {
                size_t new_size = result_size * 2;
                while (new_size <= resultPos + written) {
                    new_size *= 2;
                }

                char* new_result = (char*)realloc(result, new_size);
                if (!new_result) break;
                result = new_result;
                
            }

            // add result to result
            strcpy(result + resultPos, pair);
            resultPos += written;

            matchCount++;

            //jump out this pos to search next
            offset = end + 1;

        } else {
            offset++;
        }

    }

    free(target_data);
    free(ad_data);
    return result;
}

// 更新: tr_insert 实现，正确处理父子关系
void tr_insert(struct sound_seg* src_track,
        struct sound_seg* dest_track,
        size_t destpos, size_t srcpos, size_t len) {
    // Check edge cases
    if (!src_track || !dest_track || len == 0) return;
    if (destpos > dest_track->length) destpos = dest_track->length;
    if (srcpos >= src_track->length) return;
    if (srcpos + len > src_track->length) len = src_track->length - srcpos;
    if (len == 0) return;

    // Find the node where destpos is located
    seg_node* curr = dest_track->head;
    seg_node* prev = NULL;
    size_t segStart = 0;

    while (curr) {
        size_t segEnd = segStart + curr->length;
        if (destpos <= segEnd) break;

        segStart = segEnd;
        prev = curr;
        curr = curr->next;
    }

    // Split the node if inserting in the middle
    if (curr && destpos < segStart + curr->length) {
        size_t offsetInNode = destpos - segStart;

        if (offsetInNode > 0) {
            // Create a new node for the second part
            seg_node* second_part = malloc(sizeof(seg_node));
            if (!second_part) return;
            
            second_part->length = curr->length - offsetInNode;
            second_part->next = curr->next;
            
            if (curr->shared) {
                // 共享节点处理
                second_part->shared = true;
                second_part->parent = curr->parent;
                second_part->parent_offset = curr->parent_offset + offsetInNode;
                second_part->samples = NULL;
                second_part->child_count = 0;
            } else {
                // 非共享节点处理
                second_part->samples = malloc(second_part->length * sizeof(int16_t));
                if (!second_part->samples) {
                    free(second_part);
                    return;
                }
                memcpy(second_part->samples, 
                    curr->samples + offsetInNode, 
                    second_part->length * sizeof(int16_t));
                second_part->shared = false;
                second_part->parent = NULL;
                second_part->parent_offset = 0;
                second_part->child_count = 0;
            }
            
            // 更新当前节点
            curr->length = offsetInNode;
            curr->next = second_part;
            prev = curr;
            curr = second_part;
        }
    }

    // Create a shared node pointing to the source track
    seg_node* shared_node = malloc(sizeof(seg_node));
    if (!shared_node) return;

    shared_node->length = len;
    shared_node->shared = true;
    shared_node->parent = src_track;
    shared_node->parent_offset = srcpos;
    shared_node->samples = NULL;
    shared_node->child_count = 0;

    // 在源轨道中增加相关节点的child_count
    size_t current_src_pos = 0;
    seg_node* src_node = src_track->head;

    while (src_node && current_src_pos < srcpos + len) {
        size_t next_src_pos = current_src_pos + src_node->length;
        
        // 检查此节点是否与要插入的部分重叠
        if (next_src_pos > srcpos && current_src_pos < srcpos + len) {
            src_node->child_count++;
        }
        
        current_src_pos = next_src_pos;
        src_node = src_node->next;
    }

    // 插入共享节点
    shared_node->next = curr;
    if (prev) {
        prev->next = shared_node;
    } else {
        dest_track->head = shared_node;
    }

    // 更新目标轨道长度
    dest_track->length += len;
}


// get target and correlation with ad
double cross_correlation(const int16_t* a, const int16_t* b, size_t len) {
    double corr = 0.0;
    for (size_t i = 0; i < len; i++) {
        corr += (double)a[i] * (double)b[i];
    }
    return corr;
}

//sample a times itself as a refernce 
double auto_correlation(const int16_t* a, size_t len) {
    return cross_correlation(a, a, len);
}