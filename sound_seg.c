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
    // if the pointer is null return
    if (!track) return;

    seg_node* curr = track->head;
    // free the memory if its not null
    while (curr) {
        seg_node* next = curr->next;
        
        // 更新: 如果是共享节点，减少父节点的子计数
        if (curr->shared && curr->parent) {
            seg_node* parent_node = curr->parent->head;
            size_t parent_offset = curr->parent_offset;
            size_t curr_pos = 0;
            
            // 找到父节点中对应的节点
            while (parent_node) {
                size_t next_pos = curr_pos + parent_node->length;
                if (parent_offset < next_pos) {
                    // 找到包含该偏移量的父节点
                    parent_node->child_count--;
                    break;
                }
                curr_pos = next_pos;
                parent_node = parent_node->next;
            }
        }
        
        // 更新: 释放样本条件修改
        if (curr->samples && (!curr->shared || (curr->shared && curr->child_count == 0))) {
            free(curr->samples);
        }

        free(curr);
        curr = next;
    }

    free(track);

    return;
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    if (!seg) return 0;
    return seg->length;
}

// 更新: 辅助函数，找到指定位置所在的节点
static seg_node* find_node_at_position(struct sound_seg* track, size_t pos, size_t* offset_in_track) {
    if (!track || !track->head) return NULL;
    
    seg_node* curr = track->head;
    size_t curr_pos = 0;
    
    while (curr) {
        if (curr_pos + curr->length > pos) {
            // 找到了包含该位置的节点
            if (offset_in_track) *offset_in_track = curr_pos;
            return curr;
        }
        curr_pos += curr->length;
        curr = curr->next;
    }
    
    return NULL; // 未找到
}

// 更新: 辅助函数，获取父节点中的实际数据
static int16_t get_actual_sample(seg_node* node, size_t offset_in_node) {
    if (!node->shared) {
        // 非共享节点直接返回自己的数据
        return node->samples[offset_in_node];
    }
    
    // 共享节点递归获取父节点数据
    struct sound_seg* parent = node->parent;
    size_t parent_pos = node->parent_offset + offset_in_node;
    size_t parent_offset;
    seg_node* parent_node = find_node_at_position(parent, parent_pos, &parent_offset);
    
    if (parent_node) {
        return get_actual_sample(parent_node, parent_pos - parent_offset);
    }
    
    // 异常情况，不应该发生
    return 0;
}

// 更新: 辅助函数，设置实际样本数据，确保更改传播到相关父子节点
static void set_actual_sample(seg_node* node, size_t offset_in_node, int16_t value) {
    if (!node->shared) {
        // 非共享节点直接设置自己的数据
        node->samples[offset_in_node] = value;
        return;
    }
    
    // 共享节点，更新父节点数据
    struct sound_seg* parent = node->parent;
    size_t parent_pos = node->parent_offset + offset_in_node;
    size_t parent_offset;
    seg_node* parent_node = find_node_at_position(parent, parent_pos, &parent_offset);
    
    if (parent_node) {
        set_actual_sample(parent_node, parent_pos - parent_offset, value);
    }
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    // Check edge cases
    if (!track || !dest) return;
    if (pos >= track->length) return;

    // Calculate how many elements can be read
    size_t can_read = track->length - pos;
    if (len > can_read) len = can_read;

    // 重新实现: 直接遍历并读取数据
    for (size_t i = 0; i < len; i++) {
        size_t curr_offset;
        seg_node* node = find_node_at_position(track, pos + i, &curr_offset);
        if (node) {
            size_t offset_in_node = (pos + i) - curr_offset;
            
            // 使用辅助函数获取实际数据，处理共享节点
            if (node->shared) {
                dest[i] = get_actual_sample(node, offset_in_node);
            } else {
                dest[i] = node->samples[offset_in_node];
            }
        }
    }
}

// 更新: tr_write 重新实现，确保父子关系中的数据正确传播
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;

    // If position is beyond track length, set pos to the end
    if (pos > track->length) pos = track->length;
    
    // Case 1: If writing at the end of the track, append a new node
    if (pos == track->length) {
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
        newNode->parent_offset = 0;
        newNode->child_count = 0;  // 初始化子节点计数为0
        newNode->next = NULL;
        
        // Add to the end of the list
        if (!track->head) {
            track->head = newNode;
        } else {
            seg_node* current = track->head;
            while (current->next) {
                current = current->next;
            }
            current->next = newNode;
        }
        
        track->length += len;
        return;
    }
    
    // Case 2: Writing within existing track
    for (size_t i = 0; i < len; i++) {
        if (pos + i >= track->length) {
            // 如果写入超出了当前轨道长度，创建一个新节点
            size_t remaining = len - i;
            seg_node* newNode = malloc(sizeof(seg_node));
            if (!newNode) return;
            
            newNode->samples = malloc(remaining * sizeof(int16_t));
            if (!newNode->samples) {
                free(newNode);
                return;
            }
            
            memcpy(newNode->samples, src + i, remaining * sizeof(int16_t));
            newNode->length = remaining;
            newNode->shared = false;
            newNode->parent = NULL;
            newNode->parent_offset = 0;
            newNode->child_count = 0;
            newNode->next = NULL;
            
            // 添加到列表末尾
            if (!track->head) {
                track->head = newNode;
            } else {
                seg_node* current = track->head;
                while (current->next) {
                    current = current->next;
                }
                current->next = newNode;
            }
            
            track->length += remaining;
            break;
        } else {
            // 更新现有数据
            size_t curr_offset;
            seg_node* node = find_node_at_position(track, pos + i, &curr_offset);
            if (node) {
                size_t offset_in_node = (pos + i) - curr_offset;
                
                // 对于共享节点，更新父节点数据
                if (node->shared) {
                    set_actual_sample(node, offset_in_node, src[i]);
                } else {
                    node->samples[offset_in_node] = src[i];
                }
            }
        }
    }
}

// 更新: 检查是否节点是父节点（有子节点）
bool is_parent_node(seg_node* node) {
    return node->child_count > 0;
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    // Check edge cases
    if (!track || !track->head) return false;
    if (pos >= track->length) return false;
    if (pos + len > track->length) len = track->length - pos;
    if (len == 0) return true;  // Nothing to delete

    // 检查要删除的范围内是否有父节点
    for (size_t i = 0; i < len; i++) {
        size_t curr_offset;
        seg_node* node = find_node_at_position(track, pos + i, &curr_offset);
        if (node && is_parent_node(node)) {
            // 如果是父节点，不能删除
            return false;
        }
    }

    size_t offset = 0;
    size_t deleted = 0;
    seg_node* prev = NULL;
    seg_node* node = track->head;

    // Find the starting node for deletion
    while (node && offset + node->length <= pos) {
        offset += node->length;
        prev = node;
        node = node->next;
    }

    // Process deletion across potentially multiple nodes
    while (node && deleted < len) {
        size_t node_start = (pos > offset) ? (pos - offset) : 0;
        size_t remainingToDelete = len - deleted;
        size_t nodeDeleteLen = (node_start + remainingToDelete <= node->length) ? 
                               remainingToDelete : (node->length - node_start);

        // Cannot delete if node is a parent (has children)
        if (is_parent_node(node)) {
            return false;
        }

        // Case 1: Delete entire node
        if (node_start == 0 && nodeDeleteLen == node->length) {
            seg_node* toDelete = node;
            node = node->next;

            if (prev) {
                prev->next = node;
            } else {
                track->head = node;
            }
            
            // Free resources
            if (toDelete->samples && !toDelete->shared) {
                free(toDelete->samples);
            }
            free(toDelete);
        }
        // Case 2: Delete from beginning of node
        else if (node_start == 0) {
            if (!node->shared) {
                // Shift remaining data to the front
                memmove(node->samples, 
                        node->samples + nodeDeleteLen, 
                        (node->length - nodeDeleteLen) * sizeof(int16_t));
                node->length -= nodeDeleteLen;
            } else {
                // Create a new node that points to later part of parent
                seg_node* newNode = malloc(sizeof(seg_node));
                if (!newNode) return false;
                
                newNode->length = node->length - nodeDeleteLen;
                newNode->shared = true;
                newNode->parent = node->parent;
                newNode->parent_offset = node->parent_offset + nodeDeleteLen;
                newNode->samples = NULL;
                newNode->child_count = 0;
                newNode->next = node->next;
                
                if (prev) {
                    prev->next = newNode;
                } else {
                    track->head = newNode;
                }
                
                free(node);
                node = newNode;
            }
        }
        // Case 3: Delete from end of node
        else if (node_start + nodeDeleteLen == node->length) {
            node->length -= nodeDeleteLen;
            prev = node;
            node = node->next;
        }
        // Case 4: Delete from middle of node
        else {
            // Create a node for the part after deletion
            seg_node* afterNode = malloc(sizeof(seg_node));
            if (!afterNode) return false;
            
            size_t afterLen = node->length - node_start - nodeDeleteLen;
            
            if (!node->shared) {
                // Copy the data after deletion point
                afterNode->samples = malloc(afterLen * sizeof(int16_t));
                if (!afterNode->samples) {
                    free(afterNode);
                    return false;
                }
                
                memcpy(afterNode->samples, 
                       node->samples + node_start + nodeDeleteLen, 
                       afterLen * sizeof(int16_t));
                
                afterNode->length = afterLen;
                afterNode->shared = false;
                afterNode->parent = NULL;
                afterNode->parent_offset = 0;
                afterNode->child_count = 0;
            } else {
                // Point to the part after deletion in parent
                afterNode->samples = NULL;
                afterNode->length = afterLen;
                afterNode->shared = true;
                afterNode->parent = node->parent;
                afterNode->parent_offset = node->parent_offset + node_start + nodeDeleteLen;
                afterNode->child_count = 0;
            }
            
            afterNode->next = node->next;
            node->next = afterNode;
            node->length = node_start;
            
            prev = afterNode;
            node = afterNode->next;
        }

        deleted += nodeDeleteLen;
        offset += node_start + nodeDeleteLen;
    }

    // Update track length
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

    // Find nodes in source track that contain the portion to be inserted
    size_t src_end_pos = srcpos + len - 1;
    size_t curr_src_pos = 0;
    seg_node* src_nodes_start = NULL; // 找到第一个包含源数据的节点
    size_t src_start_offset = 0;

    // 找到源轨道中包含要插入部分的第一个节点
    seg_node* curr_src = src_track->head;
    while (curr_src) {
        size_t next_pos = curr_src_pos + curr_src->length;
        if (srcpos < next_pos) {
            src_nodes_start = curr_src;
            src_start_offset = srcpos - curr_src_pos;
            break;
        }
        curr_src_pos = next_pos;
        curr_src = curr_src->next;
    }

    if (!src_nodes_start) return; // 未找到源节点，不应发生

    // Find where to insert in destination track
    seg_node* prev_dest = NULL;
    seg_node* curr_dest = dest_track->head;
    size_t curr_dest_pos = 0;

    while (curr_dest && curr_dest_pos + curr_dest->length <= destpos) {
        curr_dest_pos += curr_dest->length;
        prev_dest = curr_dest;
        curr_dest = curr_dest->next;
    }

    // Split destination node if inserting in middle
    if (curr_dest && destpos < curr_dest_pos + curr_dest->length) {
        size_t offset_in_dest = destpos - curr_dest_pos;
        
        if (offset_in_dest > 0) {
            // Create a new node for the second part
            seg_node* second_part = malloc(sizeof(seg_node));
            if (!second_part) return;
            
            // 设置第二部分节点的属性
            second_part->length = curr_dest->length - offset_in_dest;
            second_part->next = curr_dest->next;
            
            if (curr_dest->shared) {
                // 如果是共享节点，第二部分也是共享的
                second_part->shared = true;
                second_part->parent = curr_dest->parent;
                second_part->parent_offset = curr_dest->parent_offset + offset_in_dest;
                second_part->samples = NULL;
                second_part->child_count = 0;
            } else {
                // 非共享节点，需要复制数据
                second_part->samples = malloc(second_part->length * sizeof(int16_t));
                if (!second_part->samples) {
                    free(second_part);
                    return;
                }
                memcpy(second_part->samples, 
                       curr_dest->samples + offset_in_dest, 
                       second_part->length * sizeof(int16_t));
                second_part->shared = false;
                second_part->parent = NULL;
                second_part->parent_offset = 0;
                second_part->child_count = 0;
            }
            
            // 更新当前节点
            curr_dest->length = offset_in_dest;
            curr_dest->next = second_part;
            prev_dest = curr_dest;
            curr_dest = second_part;
        }
    }

    // Create shared node pointing to source
    seg_node* shared_node = malloc(sizeof(seg_node));
    if (!shared_node) return;
    
    shared_node->length = len;
    shared_node->shared = true;
    shared_node->parent = src_track;
    shared_node->parent_offset = srcpos;
    shared_node->samples = NULL;
    shared_node->child_count = 0;
    
    // 在源轨道中增加子节点计数
    // 找到每个覆盖源范围的节点，并增加其子节点计数
    curr_src = src_track->head;
    curr_src_pos = 0;
    
    while (curr_src && curr_src_pos < srcpos + len) {
        size_t next_pos = curr_src_pos + curr_src->length;
        
        // 检查此节点是否包含要插入的部分数据
        if (next_pos > srcpos && curr_src_pos < srcpos + len) {
            curr_src->child_count++;
        }
        
        curr_src_pos = next_pos;
        curr_src = curr_src->next;
    }
    
    // 插入共享节点
    shared_node->next = curr_dest;
    if (prev_dest) {
        prev_dest->next = shared_node;
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