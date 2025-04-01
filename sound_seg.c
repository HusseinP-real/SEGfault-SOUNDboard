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
    //return (size_t)-1;
}

// Read len elements from position pos into dest (e in pos-> pos + len copy)
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    // Check edge cases
    if (!track || !dest) return;
    if (pos >= track->length) return;

    // Calculate how many elements can be read
    size_t can_read = track->length - pos;
    if (len > can_read) len = can_read;

    size_t totalRead = 0;
    size_t segStart = 0;

    // Iterate through the linked list
    seg_node* curr = track->head;
    while (curr && totalRead < len) {
        size_t segEnd = segStart + curr->length;
        
        if (pos < segEnd) {
            // Calculate position within current node
            size_t offsetInNode = (pos > segStart) ? (pos - segStart) : 0;
            
            // Calculate how much to read from this node
            size_t available = curr->length - offsetInNode;
            size_t toRead = (len - totalRead < available) ? (len - totalRead) : available;
            
            // Handle shared data
            if (curr->shared && curr->parent) {
                // Create a temporary buffer to read from parent track
                int16_t* temp_buffer = malloc(toRead * sizeof(int16_t));
                if (!temp_buffer) return;
                
                // Read data from parent track at the correct offset
                size_t parent_pos = curr->parent_offset + offsetInNode;
                
                // Recursively read from parent track
                tr_read(curr->parent, temp_buffer, parent_pos, toRead);
                
                // Copy data to destination buffer
                memcpy(dest + totalRead, temp_buffer, toRead * sizeof(int16_t));
                
                // Free temporary buffer
                free(temp_buffer);
            } else {
                // Direct copy for non-shared data
                memcpy(dest + totalRead, curr->samples + offsetInNode, toRead * sizeof(int16_t));
            }
            
            // Update tracking variables
            totalRead += toRead;
            pos += toRead;
        }
        
        // Move to next node
        segStart = segEnd;
        curr = curr->next;
    }
}

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
    
    // Case 2: Writing within existing nodes
    size_t totalWritten = 0;
    size_t segStart = 0;
    seg_node* curr = track->head;
    
    while (curr && totalWritten < len) {
        size_t segEnd = segStart + curr->length;
        if (pos < segEnd) {
            size_t offsetInNode = (pos > segStart) ? (pos - segStart) : 0;
            size_t available = curr->length - offsetInNode;
            size_t toWrite = (len - totalWritten < available) ? (len - totalWritten) : available;
            
            // Handle shared data
            if (curr->shared && curr->parent) {
                // Propagate write to parent track
                tr_write(curr->parent, src + totalWritten, curr->parent_offset + offsetInNode, toWrite);
            } else {
                // Direct write for non-shared data
                memcpy(curr->samples + offsetInNode, src + totalWritten, toWrite * sizeof(int16_t));
            }
            
            totalWritten += toWrite;
            pos += toWrite;
        }
        
        segStart = segEnd;
        curr = curr->next;
    }
    
    // Case 3: If there's remaining data to write and we've reached the end, create a new node
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
        newNode->parent_offset = 0;
        newNode->next = NULL;
        
        // Find the end of the list and append
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
    }
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    // Check edge cases
    if (!track || !track->head) return false;
    if (pos >= track->length) return false;
    if (pos + len > track->length) len = track->length - pos;
    if (len == 0) return true;  // Nothing to delete

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

        // Cannot delete if node is a parent (shared by other tracks)
        if (node->shared) {
            // Check if this is a parent node with children
            // This is a simplification - in a complete implementation, 
            // you would need to track children for each node
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
            } else {
                // Point to the part after deletion in parent
                afterNode->samples = NULL;
                afterNode->length = afterLen;
                afterNode->shared = true;
                afterNode->parent = node->parent;
                afterNode->parent_offset = node->parent_offset + node_start + nodeDeleteLen;
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

// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
        struct sound_seg* dest_track,
        size_t destpos, size_t srcpos, size_t len) {
    // Check edge cases
    bool self_insert = (src_track == dest_track);
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
    if (destpos < segEnd) break;

        segStart = segEnd;
        prev = curr;
        curr = curr->next;
    }

    if (curr) {
        size_t offsetInNode = destpos - segStart;
        // If inserting in the middle, split the node.
        if (offsetInNode > 0 && offsetInNode < curr->length) {
            if (self_insert) {
                // For self-insert, split the node into two shared nodes without deep copying.
                // Create head node for the portion before insertion.
                seg_node* head_node = (seg_node*)malloc(sizeof(seg_node));
                if (!head_node) return;
                head_node->length = offsetInNode;
                head_node->shared = true;
                // 如果 curr 是共享，则使用其 parent，否则使用 dest_track 自身
                head_node->parent = curr->shared ? curr->parent : dest_track;
                head_node->parent_offset = curr->shared ? curr->parent_offset : 0;
                head_node->samples = NULL;
                
                // Create tail node for the portion after insertion.
                seg_node* tail_node = (seg_node*)malloc(sizeof(seg_node));
                if (!tail_node) {
                    free(head_node);
                    return;
                }
                tail_node->length = curr->length - offsetInNode;
                tail_node->shared = true;
                tail_node->parent = curr->shared ? curr->parent : dest_track;
                tail_node->parent_offset = curr->shared ? (curr->parent_offset + offsetInNode) : offsetInNode;
                tail_node->samples = NULL;
                
                // Link the new nodes: head_node -> tail_node, and replace curr with head_node.
                head_node->next = tail_node;
                tail_node->next = curr->next;
                
                if (prev) {
                    prev->next = head_node;
                } else {
                    dest_track->head = head_node;
                }
                
                // Free the original node and set curr to head_node for further insertion.
                free(curr);
                curr = head_node;
            } else {
                // Original behavior for non self-insert: preserve shared status.
                seg_node* tail_node = (seg_node*)malloc(sizeof(seg_node));
                if (!tail_node) return;
                tail_node->length = curr->length - offsetInNode;
                tail_node->shared = curr->shared;
                tail_node->parent = curr->parent;
                if (curr->shared) {
                    tail_node->parent_offset = curr->parent_offset + offsetInNode;
                    tail_node->samples = NULL;
                } else {
                    tail_node->samples = malloc(tail_node->length * sizeof(int16_t));
                    if (!tail_node->samples) {
                        free(tail_node);
                        return;
                    }
                    memcpy(tail_node->samples, curr->samples + offsetInNode, tail_node->length * sizeof(int16_t));
                    tail_node->parent_offset = 0;
                }
                tail_node->next = curr->next;
                curr->length = offsetInNode;
                curr->next = tail_node;
            }
        }

        // Create a shared node pointing to the source track
        seg_node* shared_node = (seg_node*)malloc(sizeof(seg_node));
        if (!shared_node) return;

        // Always create a fully shared node
        shared_node->length = len;
        shared_node->shared = true;
        shared_node->parent = src_track;
        shared_node->parent_offset = srcpos;
        shared_node->samples = NULL;  // Shared nodes don't need their own samples
        shared_node->next = NULL;

        // Insert the shared node into the list
        if (!dest_track->head) {
            dest_track->head = shared_node;
        } else if (!curr) {
            if (prev) {
                prev->next = shared_node;
            } else {
                dest_track->head = shared_node;
            }
        } else {
            // Insert after the current node (which might be the first half of a split)
            shared_node->next = curr->next;
            curr->next = shared_node;
        }

        // Update the destination track length
        dest_track->length += len;
    } else if (prev) {
        // Insert at the end if we have a previous node
        seg_node* shared_node = (seg_node*)malloc(sizeof(seg_node));
        if (!shared_node) return;

        shared_node->length = len;
        shared_node->shared = true;
        shared_node->parent = src_track;
        shared_node->parent_offset = srcpos;
        shared_node->samples = NULL;
        shared_node->next = NULL;

        prev->next = shared_node;
        dest_track->length += len;
    } else {
        // Handle the case of an empty destination track
        seg_node* shared_node = (seg_node*)malloc(sizeof(seg_node));
        if (!shared_node) return;

        shared_node->length = len;
        shared_node->shared = true;
        shared_node->parent = src_track;
        shared_node->parent_offset = srcpos;
        shared_node->samples = NULL;
        shared_node->next = NULL;

        dest_track->head = shared_node;
        dest_track->length += len;
    }
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

