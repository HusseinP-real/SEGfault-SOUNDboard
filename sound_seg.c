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
    size_t child_count;
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

        //if shared
        if (curr->shared && curr->parent) {
            size_t parent_pos = 0;
            seg_node* parent_node = curr->parent->head;

            while (parent_node) {
                size_t next_pos = parent_pos + parent_node->length;
                if (curr->parent_offset >= parent_pos && curr->parent_offset < next_pos) {
                    if (parent_node->child_count > 0) {
                        parent_node->child_count--;
                    }
                    break;
                }
                parent_pos = next_pos;
                parent_node = parent_node->next;
            }
        }
        
        // release sample didn't shared and parent don't have child
        if (curr-> samples && (!curr->shared || !curr->parent)) {
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

// Read len elements from position pos into dest (e in pos-> pos + len copy)
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    //check if track samples and dest is null
    if (!track || !dest) return;
    if (pos >= track->length) return;

    size_t can_read = track->length - pos;
    if (len > can_read) len = can_read;

    //count the number have read and position
    size_t totalRead = 0;
    size_t segStart = 0; //start of each node(segment)

    //iterate node of the linked list
    seg_node* curr = track->head;

    while(curr && totalRead < len) {
        size_t segEnd = segStart + curr->length;
        
        //find what node pos in if not jump to the next node
        if (pos < segEnd) {
            size_t offsetInNode;
            if (pos > segStart) {
                offsetInNode = pos - segStart;
            } else {
                offsetInNode = 0;
            }

            size_t available = curr->length - offsetInNode; //see how much left can be read in node from offset

            size_t toRead;
            //see if available is enough if not jump to the next node to read left
            if (len - totalRead < available) {
                toRead = len - totalRead;
            } else {
                toRead = available;
            }

            //check if the data is shared
            if (curr->shared && curr->parent) {
                //find the node of parent
                size_t parent_pos = 0;
                seg_node* parent_curr = curr->parent->head;
                size_t parent_offset = curr->parent_offset + offsetInNode;
                
                while (parent_curr) {
                    size_t next_pos = parent_pos + parent_curr->length;
                    if(parent_offset < next_pos) {
                        size_t offsetInParent = parent_offset - parent_pos;
                        if (parent_curr->shared) {
                            memcpy(dest + totalRead, parent_curr->samples + offsetInParent, toRead * sizeof(int16_t));
                        } else {
                            memcpy(dest + totalRead, parent_curr->samples + offsetInParent, toRead * sizeof(int16_t));
                        }

                        break;
                    
                    }

                    parent_pos = next_pos;
                    parent_curr = parent_curr->next;
                }

                //if cannot find
                if(!parent_curr) {
                    memset(dest + totalRead, 0, toRead * sizeof(int16_t));
                }
            } else {
                //not shared node read
                memcpy(dest + totalRead, curr->samples + offsetInNode, toRead * sizeof(int16_t));
            }

            //update the the position and read
            totalRead += toRead;
            pos += toRead;
        }
        segStart = segEnd;
        curr = curr->next;
    }

    return;
}

void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;

    // if position is greater than track length, set pos as the end of the track
    if (pos > track->length) pos = track->length;
    
    size_t totalWritten = 0;
    size_t segStart = 0;
    seg_node* curr = track->head;
    
    // If pos equals track->length or linked list is empty, append a new node at tail
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
        newNode->child_count = 0;
        
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
    
    // Iterate through the linked list to write data
    while (curr && totalWritten < len) {
        size_t segEnd = segStart + curr->length;
        if (pos < segEnd) {
            size_t offsetInNode;
            if (pos > segStart)
                offsetInNode = pos - segStart;
            else
                offsetInNode = 0;
            
            size_t available = curr->length - offsetInNode;
            size_t toWrite;
            if (len - totalWritten < available)
                toWrite = len - totalWritten;
            else
                toWrite = available;
            
            //if current node is shared
            if (curr->shared && curr->parent) {
                size_t parent_pos = 0;
                seg_node* parent_node = curr->parent->head;
                size_t parent_offset = curr->parent_offset + offsetInNode;

                while(parent_node) {
                    size_t next_pos = parent_pos + parent_node->length;
                    if (parent_offset < next_pos) {
                        size_t offsetInParent = parent_offset - parent_pos;
                        if (parent_node->shared) {
                            memcpy(parent_node->samples + offsetInParent, src + totalWritten, toWrite * sizeof(int16_t));
                        } else {
                            memcpy(parent_node->samples + offsetInParent, src + totalWritten, toWrite * sizeof(int16_t));
                        }
                        break;
                    }
                    parent_pos = next_pos;
                    parent_node = parent_node->next;
                }
            

            } else {
                memcpy(curr->samples + offsetInNode, src + totalWritten, toWrite * sizeof(int16_t));
            }

            totalWritten += toWrite;
            pos += toWrite;
        }

        segStart = segEnd;
        curr = curr->next;

    }
    
    // If there's still data remaining to write, create a new node for the remaining part
    if (totalWritten < len) {
        size_t remaining = len - totalWritten;
        seg_node* newNode = malloc(sizeof(seg_node));
        if (!newNode) return;
        
        newNode->samples = malloc(remaining * sizeof(int16_t));
        if (!newNode->samples) {
            free(newNode);
            return;
        }
        memcpy(newNode->samples, src + totalWritten, remaining * sizeof(int16_t));
        newNode->length = remaining;
        newNode->shared = false;
        newNode->parent = NULL;
        newNode->next = NULL;
        newNode->parent_offset = 0;
        newNode->child_count = 0;
        
        seg_node* current = track->head;
        while (current->next) {
            current = current->next;
        }
        current->next = newNode;
        track->length += remaining;
    }
    
    return;
}


// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    //edge
    if (!track || !track->head || !track->head->samples) return false;
    if (pos >= track->length) return false;
    if (pos + len > track->length) len = track->length - pos;
    if (len == 0) return true;

    //check if delete len has father nodes
    size_t check_pos = 0;
    seg_node* curr_check = track->head;

    while (curr_check) {
        size_t next_pos = check_pos + curr_check->length;

        if (next_pos > pos && check_pos < pos + len) {
            if (curr_check->child_count > 0) {
                return false;
            }
        }
        check_pos = next_pos;
        curr_check = curr_check->next;
    }

    size_t offset = 0;
    size_t deleted = 0;
    seg_node* prev = NULL;
    seg_node* node = track->head;

    while (node && offset + node->length <= pos) {
        offset += node->length;
        prev = node;
        node = node->next;
    }

    while (node && deleted < len) {
        size_t node_start;
        if (pos > offset) {
            node_start = pos - offset;
        } else {
            node_start = 0;
        }
        size_t remainingToDeleted = len - deleted;
        size_t nodeDeletedLen;
        if (node_start + remainingToDeleted <= node->length) {
            nodeDeletedLen = remainingToDeleted;
        } else {
            nodeDeletedLen = node->length - node_start;
        }

        //if shared cannot deleted
        if (node->shared) return false;

        //delete hole node
        if (node_start == 0 && nodeDeletedLen == node->length) {
            if(node->child_count > 0 || node->shared) return false;
            seg_node* toDelete = node;
            node = node->next;

            if (prev) {
                prev->next = node;
            } else {
                track->head = node;
            }
            if (toDelete->samples && !toDelete->shared) {
                free(toDelete->samples);
            }
            free(toDelete);
        }

        //delete the head to somewhere
        else if (node_start == 0) {
            if(node->child_count > 0 || node->shared) return false;

            //if is not shared
            if (!node->shared) {
                memmove(node->samples, node->samples + nodeDeletedLen, (node->length - nodeDeletedLen) * sizeof(int16_t));
                node->length -= nodeDeletedLen;
                //if (node->parent) node->parent_offset += nodeDeletedLen;
            }       
            //if is shared node
            else {
                seg_node* new_node = (seg_node*)malloc(sizeof(seg_node));
                if (!new_node) return false;
                new_node->length = node->length - nodeDeletedLen;
                new_node->shared = node->shared;
                new_node->parent = node->parent;
                new_node->parent_offset = node->parent_offset + nodeDeletedLen;
                new_node->next = node->next;
                new_node->child_count = 0;
                new_node->samples = NULL;
                if (prev) {
                    prev->next = new_node;
                } else {
                    track->head = new_node;
                }
                free(node);
                node = new_node;
            }
        }
        //delete somewhere to tail
        else if (node_start + nodeDeletedLen == node->length) {
            if(node->child_count > 0 || node->shared) return false;
            node->length -= nodeDeletedLen;
            prev = node;
            node = node->next;
        }
        //delete middle
        else {
            if(node->child_count > 0 || node->shared) return false;

            //creat a node to store the rest
            seg_node* after_node = (seg_node*)malloc(sizeof(seg_node));
            if (!after_node) return false;
            size_t after_len = node->length - node_start - nodeDeletedLen;

            after_node->child_count = 0;

            if (!node->shared) {
                after_node->samples = malloc(after_len * sizeof(int16_t));
                if (!after_node->samples) {
                    free(after_node);
                    return false;
                }
                memcpy(after_node->samples, node->samples + node_start + nodeDeletedLen, after_len * sizeof(int16_t));
                after_node->length = after_len;
                after_node->shared = false;
                after_node->parent = NULL;
                after_node->parent_offset = 0;
            } else {
                after_node->samples = NULL;
                after_node->length = after_len;
                after_node->shared = true;
                after_node->parent = node->parent;
                after_node->parent_offset = node->parent_offset + node_start + nodeDeletedLen;
            }
            after_node->next = node->next;
            node->next = after_node;
            node->length = node_start;
            prev = after_node;
            node = after_node->next;

        }

        deleted += nodeDeletedLen;
        offset = offset + node_start + nodeDeletedLen;

        pos += nodeDeletedLen;

    }

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

// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
            struct sound_seg* dest_track,
            size_t destpos, size_t srcpos, size_t len) {
    //check egde
    if (!src_track || !dest_track || len == 0) return;
    if (destpos > dest_track->length) destpos = dest_track->length;
    if (srcpos >= src_track->length) return;
    if (srcpos + len > src_track->length) len = src_track->length - srcpos;
    if (len == 0) return;

    seg_node* curr = dest_track->head; //iterate
    seg_node* prev = NULL;
    size_t segStart = 0;

    //iterate nodes of ll until find the pos
    while (curr) {
        size_t segEnd = segStart + curr->length;
        if (destpos < segEnd) break;
        
        segStart = segEnd;
        prev = curr;
        curr = curr->next;

    }

    //middle
    if (curr && destpos > segStart) {
        size_t offsetInNode = destpos - segStart;

        seg_node* tail_node = malloc(sizeof(seg_node));
        if (!tail_node) return;

        if (curr->shared || curr->child_count > 0) {
            free(tail_node);
            return;
        }
    

        tail_node->length = curr->length - offsetInNode;
        tail_node->shared = curr->shared;
        tail_node->parent = curr->parent;
        tail_node->parent_offset = curr->parent_offset + offsetInNode;
        tail_node->next = curr->next;
        tail_node->child_count = 0;

        if (curr->shared) {
            tail_node->samples = NULL;
        } else {
            tail_node->samples = malloc(tail_node->length * sizeof(int16_t));
            if (!tail_node->samples) {
                free(tail_node);
                return;
            }
            memcpy(tail_node->samples, curr->samples + offsetInNode, tail_node->length *sizeof(int16_t));
        }

        curr->length = offsetInNode;
        curr->next = tail_node;
        prev = curr;
        curr = tail_node;
    }

    //creat shared node
    seg_node* shared_node = (seg_node*)malloc(sizeof(seg_node));
    if (!shared_node) return;
    shared_node->length = len;
    shared_node->shared = true;
    shared_node->parent = (sound_seg*)src_track;
    shared_node->parent_offset = srcpos;
    shared_node->next = curr;
    shared_node->samples = NULL;
    shared_node->child_count = 0;

    if (prev) {
        prev->next = shared_node;
    } else {
        dest_track->head = shared_node;
    }

    size_t curr_scr_pos = 0;
    seg_node* src_node = src_track->head;

    while(src_node) {
        size_t next_pos = curr_scr_pos + src_node->length;

        if (next_pos > srcpos && curr_scr_pos < srcpos + len) {
            src_node->child_count++;
        }

        curr_scr_pos =next_pos;
        src_node = src_node->next;

    }
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