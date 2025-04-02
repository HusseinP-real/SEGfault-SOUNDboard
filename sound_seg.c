#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 每个音频样本是一个独立的结构体
typedef struct sample {
    int16_t value;                 // 样本值
    struct sample* parent;         // 如果是子样本，指向父样本
    struct sound_seg* parent_track; // 父样本所在的轨道
    size_t parent_pos;             // 父样本在其轨道中的位置
    bool has_children;             // 标识此样本是否有子样本
} sample;

// 音轨是样本的数组
struct sound_seg {
    sample** samples;              // 样本数组
    size_t length;                 // 轨道长度（样本数量）
    size_t capacity;               // 样本数组的容量
};

// 辅助函数：创建新样本
static sample* create_sample(int16_t value) {
    sample* new_sample = (sample*)malloc(sizeof(sample));
    if (!new_sample) return NULL;
    
    new_sample->value = value;
    new_sample->parent = NULL;
    new_sample->parent_track = NULL;
    new_sample->parent_pos = 0;
    new_sample->has_children = false;
    
    return new_sample;
}

// 辅助函数：确保轨道有足够容量
static bool ensure_capacity(struct sound_seg* track, size_t required_capacity) {
    if (!track) return false;
    
    if (track->capacity >= required_capacity) return true;
    
    // 如果需要更多容量，则增加到下一个2的幂
    size_t new_capacity = track->capacity > 0 ? track->capacity : 1;
    while (new_capacity < required_capacity) {
        new_capacity *= 2;
    }
    
    // 重新分配内存
    sample** new_samples = (sample**)realloc(track->samples, new_capacity * sizeof(sample*));
    if (!new_samples) return false;
    
    track->samples = new_samples;
    track->capacity = new_capacity;
    return true;
}

// 辅助函数：将写入传播到所有相关样本
static void propagate_write(sample* samp, int16_t value) {
    if (!samp) return;
    
    // 设置当前样本的值
    samp->value = value;
    
    // 传播到父样本
    if (samp->parent) {
        samp->parent->value = value;
    }
    
    // 传播到同一父样本的其他子样本（"兄弟"样本）
    if (samp->parent && samp->parent_track) {
        struct sound_seg* parent_track = samp->parent_track;
        size_t parent_pos = samp->parent_pos;
        
        // 查找所有引用同一父样本的样本并更新
        for (size_t i = 0; i < parent_track->length; i++) {
            sample* other = parent_track->samples[i];
            if (other->parent && other != samp && 
                other->parent_track == samp->parent_track && 
                other->parent_pos == parent_pos) {
                other->value = value;
            }
        }
    }
}

// 从WAV文件加载到缓冲区
void wav_load(const char* fname, int16_t* dest) {
    FILE* f = fopen(fname, "rb");
    if (!f) return;
    
    // 跳过WAV头部（44字节）
    fseek(f, 44, SEEK_SET);
    
    // 计算数据大小
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    long dataSize = fileSize - 44;
    
    // 读取数据
    fseek(f, 44, SEEK_SET);
    fread(dest, 1, dataSize, f);
    
    fclose(f);
}

// 从缓冲区创建/写入WAV文件
void wav_save(const char* fname, const int16_t* src, size_t len) {
    FILE* f = fopen(fname, "wb");
    if (!f) return;
    
    // 计算数据大小
    uint32_t dataSize = (uint32_t)(len * sizeof(int16_t));
    uint32_t chunkSize = 36 + dataSize;
    
    // 写入RIFF头部
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunkSize, sizeof(uint32_t), 1, f);
    fwrite("WAVE", 1, 4, f);
    
    // 写入格式块
    fwrite("fmt ", 1, 4, f);
    uint32_t subchunk1Size = 16; // PCM
    fwrite(&subchunk1Size, sizeof(uint32_t), 1, f);
    
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = 1; // 单声道
    uint32_t sampleRate = 8000; // 8kHz
    uint16_t bitsPerSample = 16; // 16位
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    uint32_t byteRate = sampleRate * blockAlign;
    
    fwrite(&audioFormat, sizeof(uint16_t), 1, f);
    fwrite(&numChannels, sizeof(uint16_t), 1, f);
    fwrite(&sampleRate, sizeof(uint32_t), 1, f);
    fwrite(&byteRate, sizeof(uint32_t), 1, f);
    fwrite(&blockAlign, sizeof(uint16_t), 1, f);
    fwrite(&bitsPerSample, sizeof(uint16_t), 1, f);
    
    // 写入数据块
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, sizeof(uint32_t), 1, f);
    fwrite(src, sizeof(int16_t), len, f);
    
    fclose(f);
}

// 初始化新的sound_seg对象
struct sound_seg* tr_init() {
    struct sound_seg* track = (struct sound_seg*)malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    
    track->samples = NULL;
    track->length = 0;
    track->capacity = 0;
    
    return track;
}

// 销毁sound_seg对象并释放所有分配的内存
void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    
    // 释放所有样本
    for (size_t i = 0; i < track->length; i++) {
        if (track->samples[i]) {
            // 在释放内存前，清除父子关系
            if (track->samples[i]->parent) {
                track->samples[i]->parent->has_children = false;
            }
            
            free(track->samples[i]);
        }
    }
    
    // 释放样本数组
    free(track->samples);
    
    // 释放轨道结构体
    free(track);
}

// 返回段的长度
size_t tr_length(struct sound_seg* track) {
    if (!track) return 0;
    return track->length;
}

// 从位置pos读取len个元素到dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->length) return;
    
    // 调整len，如果它会超出轨道末尾
    if (pos + len > track->length) {
        len = track->length - pos;
    }
    
    // 复制值到目标缓冲区
    for (size_t i = 0; i < len; i++) {
        dest[i] = track->samples[pos + i]->value;
    }
}

// 从src写入len个元素到位置pos
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    
    // 如果位置超出当前长度，调整为追加
    if (pos > track->length) {
        pos = track->length;
    }
    
    // 确保有足够容量
    if (!ensure_capacity(track, pos + len)) return;
    
    // 如果写入位置在当前末尾（追加）
    if (pos == track->length) {
        // 为每个新样本创建新对象
        for (size_t i = 0; i < len; i++) {
            sample* new_sample = create_sample(src[i]);
            if (!new_sample) return;
            
            track->samples[track->length++] = new_sample;
        }
    } else {
        // 写入现有位置
        for (size_t i = 0; i < len; i++) {
            if (pos + i < track->length) {
                // 现有样本 - 传播写入
                propagate_write(track->samples[pos + i], src[i]);
            } else {
                // 新样本 - 创建并追加
                sample* new_sample = create_sample(src[i]);
                if (!new_sample) return;
                
                track->samples[track->length++] = new_sample;
            }
        }
    }
}

// 从轨道中删除一系列元素
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos >= track->length) return false;
    
    // 调整len，如果它会超出轨道末尾
    if (pos + len > track->length) {
        len = track->length - pos;
    }
    
    // 检查范围内的样本是否有子样本
    for (size_t i = 0; i < len; i++) {
        if (track->samples[pos + i]->has_children) {
            return false; // 不能删除有子样本的样本
        }
    }
    
    // 释放被删除的样本
    for (size_t i = 0; i < len; i++) {
        free(track->samples[pos + i]);
    }
    
    // 移动剩余样本以填补空缺
    memmove(&track->samples[pos], &track->samples[pos + len], 
            (track->length - pos - len) * sizeof(sample*));
    
    // 更新轨道长度
    track->length -= len;
    
    return true;
}

// 辅助函数：计算互相关
double cross_correlation(const int16_t* a, const int16_t* b, size_t len) {
    double sum = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum += (double)a[i] * (double)b[i];
    }
    return sum;
}

// 辅助函数：计算自相关
double auto_correlation(const int16_t* a, size_t len) {
    return cross_correlation(a, a, len);
}

// 返回包含目标中<start>,<end>广告对的字符串
char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad) {
    if (!target || !ad) {
        char* result = (char*)malloc(1);
        if (result) result[0] = '\0';
        return result;
    }
    
    size_t target_len = tr_length((struct sound_seg*)target);
    size_t ad_len = tr_length((struct sound_seg*)ad);
    
    if (ad_len == 0 || target_len == 0 || ad_len > target_len) {
        char* result = (char*)malloc(1);
        if (result) result[0] = '\0';
        return result;
    }
    
    // 将样本提取到连续数组中进行处理
    int16_t* target_samples = (int16_t*)malloc(target_len * sizeof(int16_t));
    int16_t* ad_samples = (int16_t*)malloc(ad_len * sizeof(int16_t));
    
    if (!target_samples || !ad_samples) {
        free(target_samples);
        free(ad_samples);
        char* result = (char*)malloc(1);
        if (result) result[0] = '\0';
        return result;
    }
    
    tr_read((struct sound_seg*)target, target_samples, 0, target_len);
    tr_read((struct sound_seg*)ad, ad_samples, 0, ad_len);
    
    // 计算参考自相关和阈值
    double reference = auto_correlation(ad_samples, ad_len);
    double threshold = reference * 0.95;
    
    // 准备结果字符串
    size_t result_capacity = 256;
    char* result = (char*)malloc(result_capacity);
    if (!result) {
        free(target_samples);
        free(ad_samples);
        return NULL;
    }
    result[0] = '\0';
    
    size_t result_len = 0;
    size_t match_count = 0;
    size_t last_end = 0;
    
    // 搜索匹配项
    for (size_t pos = 0; pos <= target_len - ad_len; pos++) {
        // 跳过与先前匹配重叠的位置
        if (pos <= last_end) {
            continue;
        }
        
        double corr = cross_correlation(target_samples + pos, ad_samples, ad_len);
        if (corr >= threshold) {
            size_t start = pos;
            size_t end = pos + ad_len - 1;
            last_end = end;
            
            // 将匹配格式化为"start,end"
            char buffer[64];
            int len = snprintf(buffer, sizeof(buffer), "%s%zu,%zu", 
                               (match_count > 0) ? "\n" : "", start, end);
            
            // 确保我们有足够的空间
            if (result_len + len + 1 > result_capacity) {
                result_capacity *= 2;
                char* new_result = (char*)realloc(result, result_capacity);
                if (!new_result) {
                    free(result);
                    free(target_samples);
                    free(ad_samples);
                    return NULL;
                }
                result = new_result;
            }
            
            // 追加到结果
            strcat(result, buffer);
            result_len += len;
            match_count++;
        }
    }
    
    free(target_samples);
    free(ad_samples);
    return result;
}

// 将src_track的一部分插入到dest_track的位置destpos
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0) return;
    
    // 验证位置
    if (srcpos >= src_track->length) return;
    if (destpos > dest_track->length) destpos = dest_track->length;
    
    // 调整len，如果它会超出src_track的末尾
    if (srcpos + len > src_track->length) {
        len = src_track->length - srcpos;
    }
    
    // 确保目标轨道有足够空间
    if (!ensure_capacity(dest_track, dest_track->length + len)) return;
    
    // 为插入腾出空间
    if (destpos < dest_track->length) {
        memmove(&dest_track->samples[destpos + len], &dest_track->samples[destpos],
                (dest_track->length - destpos) * sizeof(sample*));
    }
    
    // 插入新样本
    for (size_t i = 0; i < len; i++) {
        sample* new_sample = create_sample(src_track->samples[srcpos + i]->value);
        if (!new_sample) return;
        
        // 设置父子关系
        new_sample->parent = src_track->samples[srcpos + i];
        new_sample->parent_track = src_track;
        new_sample->parent_pos = srcpos + i;
        src_track->samples[srcpos + i]->has_children = true;
        
        // 将新样本放入目标轨道
        dest_track->samples[destpos + i] = new_sample;
    }
    
    dest_track->length += len;
}