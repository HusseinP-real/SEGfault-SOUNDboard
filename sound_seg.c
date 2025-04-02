#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// A node represents a single sample
typedef struct node {
    int16_t value;             // The sample value
    struct node* next;         // Next sample in the track
    struct node* parent;       // If this is a child, points to parent node
    struct node* child;        // If this is a parent, points to first child node
    struct node* sibling;      // Next child of the same parent
    bool has_children;         // Flag indicating if this node has children
} node;

// A sound segment is a linked list of nodes
struct sound_seg {
    node* head;                // First node in the track
    size_t length;             // Number of samples in the track
};

// Helper function to find a node at a specific position
static node* find_node_at_position(struct sound_seg* track, size_t pos) {
    if (!track || !track->head || pos >= track->length) {
        return NULL;
    }
    
    node* current = track->head;
    size_t current_pos = 0;
    
    while (current && current_pos < pos) {
        current = current->next;
        current_pos++;
    }
    
    return current;
}

// Helper function to create a new node
static node* create_node(int16_t value) {
    node* new_node = (node*)malloc(sizeof(node));
    if (!new_node) return NULL;
    
    new_node->value = value;
    new_node->next = NULL;
    new_node->parent = NULL;
    new_node->child = NULL;
    new_node->sibling = NULL;
    new_node->has_children = false;
    
    return new_node;
}

// Helper function to propagate a write to all related nodes
static void propagate_write(node* start_node, int16_t value) {
    if (!start_node) return;
    
    // Track visited nodes to avoid infinite loops
    node* visited[1000] = {NULL}; // Using fixed size for simplicity
    size_t visited_count = 0;
    
    // Start with the given node
    node* stack[1000] = {start_node}; // Using fixed size for simplicity
    size_t stack_count = 1;
    
    while (stack_count > 0) {
        // Pop a node from the stack
        node* current = stack[--stack_count];
        
        // Check if we've visited this node before
        bool already_visited = false;
        for (size_t i = 0; i < visited_count; i++) {
            if (visited[i] == current) {
                already_visited = true;
                break;
            }
        }
        
        if (already_visited) continue;
        
        // Mark as visited
        visited[visited_count++] = current;
        
        // Update the value
        current->value = value;
        
        // Add parent to the stack if it exists
        if (current->parent && stack_count < 1000) {
            stack[stack_count++] = current->parent;
        }
        
        // Add children to the stack if they exist
        node* child = current->child;
        while (child && stack_count < 1000) {
            stack[stack_count++] = child;
            child = child->sibling;
        }
    }
}

// Load a WAV file into buffer
void wav_load(const char* fname, int16_t* dest) {
    FILE* f = fopen(fname, "rb");
    if (!f) return;
    
    // Skip the WAV header (44 bytes)
    fseek(f, 44, SEEK_SET);
    
    // Calculate data size
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    long dataSize = fileSize - 44;
    
    // Read the data
    fseek(f, 44, SEEK_SET);
    fread(dest, 1, dataSize, f);
    
    fclose(f);
}

// Create/write a WAV file from buffer
void wav_save(const char* fname, const int16_t* src, size_t len) {
    FILE* f = fopen(fname, "wb");
    if (!f) return;
    
    // Calculate data size
    uint32_t dataSize = (uint32_t)(len * sizeof(int16_t));
    uint32_t chunkSize = 36 + dataSize;
    
    // Write RIFF header
    fwrite("RIFF", 1, 4, f);
    fwrite(&chunkSize, sizeof(uint32_t), 1, f);
    fwrite("WAVE", 1, 4, f);
    
    // Write format chunk
    fwrite("fmt ", 1, 4, f);
    uint32_t subchunk1Size = 16; // PCM
    fwrite(&subchunk1Size, sizeof(uint32_t), 1, f);
    
    uint16_t audioFormat = 1; // PCM
    uint16_t numChannels = 1; // Mono
    uint32_t sampleRate = 8000; // 8kHz
    uint16_t bitsPerSample = 16; // 16-bit
    uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    uint32_t byteRate = sampleRate * blockAlign;
    
    fwrite(&audioFormat, sizeof(uint16_t), 1, f);
    fwrite(&numChannels, sizeof(uint16_t), 1, f);
    fwrite(&sampleRate, sizeof(uint32_t), 1, f);
    fwrite(&byteRate, sizeof(uint32_t), 1, f);
    fwrite(&blockAlign, sizeof(uint16_t), 1, f);
    fwrite(&bitsPerSample, sizeof(uint16_t), 1, f);
    
    // Write data chunk
    fwrite("data", 1, 4, f);
    fwrite(&dataSize, sizeof(uint32_t), 1, f);
    fwrite(src, sizeof(int16_t), len, f);
    
    fclose(f);
}

// Initialize a new sound_seg object
struct sound_seg* tr_init() {
    struct sound_seg* track = (struct sound_seg*)malloc(sizeof(struct sound_seg));
    if (!track) return NULL;
    
    track->head = NULL;
    track->length = 0;
    
    return track;
}

// Destroy a sound_seg object and free all allocated memory
void tr_destroy(struct sound_seg* track) {
    if (!track) return;
    
    node* current = track->head;
    while (current) {
        node* next = current->next;
        
        // Remove parent-child relationships
        if (current->parent) {
            // Detach from parent
            node* parent = current->parent;
            if (parent->child == current) {
                parent->child = current->sibling;
            } else {
                node* sibling = parent->child;
                while (sibling && sibling->sibling != current) {
                    sibling = sibling->sibling;
                }
                if (sibling) {
                    sibling->sibling = current->sibling;
                }
            }
            
            // Check if parent has any children left
            if (parent->child == NULL) {
                parent->has_children = false;
            }
        }
        
        // Free the node
        free(current);
        current = next;
    }
    
    free(track);
}

// Return the length of the segment
size_t tr_length(struct sound_seg* track) {
    if (!track) return 0;
    return track->length;
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !dest || pos >= track->length) return;
    
    // Adjust len if it would read past the end of the track
    if (pos + len > track->length) {
        len = track->length - pos;
    }
    
    // Find the starting node
    node* current = find_node_at_position(track, pos);
    if (!current) return;
    
    // Copy the values to the destination buffer
    for (size_t i = 0; i < len && current; i++) {
        dest[i] = current->value;
        current = current->next;
    }
}

// Write len elements from src into position pos
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len) {
    if (!track || !src || len == 0) return;
    
    // If position is beyond current length, adjust to append
    if (pos > track->length) {
        pos = track->length;
    }
    
    // Case 1: Writing to an empty track or appending
    if (track->length == 0 || pos == track->length) {
        node* prev = NULL;
        if (track->length > 0) {
            // Find the last node
            prev = find_node_at_position(track, track->length - 1);
        }
        
        // Create new nodes for each sample
        for (size_t i = 0; i < len; i++) {
            node* new_node = create_node(src[i]);
            if (!new_node) return;
            
            if (prev) {
                prev->next = new_node;
            } else {
                track->head = new_node;
            }
            
            prev = new_node;
        }
        
        track->length += len;
        return;
    }
    
    // Case 2: Writing at some position in the middle
    node* current = find_node_at_position(track, pos);
    
    for (size_t i = 0; i < len; i++) {
        if (!current) {
            // We've run out of nodes, append a new one
            node* new_node = create_node(src[i]);
            if (!new_node) return;
            
            node* last = find_node_at_position(track, track->length - 1);
            if (last) {
                last->next = new_node;
            } else {
                track->head = new_node;
            }
            
            track->length++;
            current = new_node;
        } else {
            // Propagate the write to all related nodes
            propagate_write(current, src[i]);
            current = current->next;
        }
    }
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || pos >= track->length) return false;
    
    // Adjust len if it would delete past the end of the track
    if (pos + len > track->length) {
        len = track->length - pos;
    }
    
    // Find the nodes just before and at the deletion range
    node* prev = (pos > 0) ? find_node_at_position(track, pos - 1) : NULL;
    node* start = find_node_at_position(track, pos);
    
    // Check if any node in the range has children
    node* current = start;
    for (size_t i = 0; i < len && current; i++) {
        if (current->has_children) {
            return false; // Can't delete a parent node
        }
        current = current->next;
    }
    
    // Find the node after the deletion range
    node* end = start;
    for (size_t i = 0; i < len && end; i++) {
        end = end->next;
    }
    
    // Remove the nodes from the track
    if (prev) {
        prev->next = end;
    } else {
        track->head = end;
    }
    
    // Disconnect nodes from their parents
    current = start;
    while (current != end) {
        node* next = current->next;
        
        if (current->parent) {
            // Detach from parent
            node* parent = current->parent;
            if (parent->child == current) {
                parent->child = current->sibling;
            } else {
                node* sibling = parent->child;
                while (sibling && sibling->sibling != current) {
                    sibling = sibling->sibling;
                }
                if (sibling) {
                    sibling->sibling = current->sibling;
                }
            }
            
            // Check if parent has any children left
            if (parent->child == NULL) {
                parent->has_children = false;
            }
        }
        
        free(current);
        current = next;
    }
    
    track->length -= len;
    return true;
}

// Helper function for calculating cross-correlation
double cross_correlation(const int16_t* a, const int16_t* b, size_t len) {
    double sum = 0.0;
    for (size_t i = 0; i < len; i++) {
        sum += (double)a[i] * (double)b[i];
    }
    return sum;
}

// Helper function for calculating auto-correlation
double auto_correlation(const int16_t* a, size_t len) {
    return cross_correlation(a, a, len);
}

// Returns a string containing <start>,<end> ad pairs in target
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
    
    // Extract samples into contiguous arrays for processing
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
    
    // Calculate reference auto-correlation and threshold
    double reference = auto_correlation(ad_samples, ad_len);
    double threshold = reference * 0.95;
    
    // Prepare result string
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
    
    // Search for matches
    for (size_t pos = 0; pos <= target_len - ad_len; pos++) {
        // Skip positions that would overlap with previous matches
        if (pos <= last_end) {
            continue;
        }
        
        double corr = cross_correlation(target_samples + pos, ad_samples, ad_len);
        if (corr >= threshold) {
            size_t start = pos;
            size_t end = pos + ad_len - 1;
            last_end = end;
            
            // Format the match as "start,end"
            char buffer[64];
            int len = snprintf(buffer, sizeof(buffer), "%s%zu,%zu", 
                               (match_count > 0) ? "\n" : "", start, end);
            
            // Ensure we have enough space
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
            
            // Append to result
            strcat(result, buffer);
            result_len += len;
            match_count++;
        }
    }
    
    free(target_samples);
    free(ad_samples);
    return result;
}

// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track,
               size_t destpos, size_t srcpos, size_t len) {
    if (!src_track || !dest_track || len == 0) return;
    
    // Validate positions
    if (srcpos >= src_track->length) return;
    if (destpos > dest_track->length) destpos = dest_track->length;
    
    // Adjust len if it would read past the end of src_track
    if (srcpos + len > src_track->length) {
        len = src_track->length - srcpos;
    }
    
    // Find the source nodes to be inserted
    node* src_start = find_node_at_position(src_track, srcpos);
    if (!src_start) return;
    
    // Find the destination position
    node* dest_prev = (destpos > 0) ? find_node_at_position(dest_track, destpos - 1) : NULL;
    node* dest_next = destpos < dest_track->length ? find_node_at_position(dest_track, destpos) : NULL;
    
    // Insert nodes at the destination position
    node* src_current = src_start;
    node* dest_prev_updated = dest_prev;
    
    for (size_t i = 0; i < len && src_current; i++) {
        // Create a new node that references the source node
        node* new_node = create_node(src_current->value);
        if (!new_node) return;
        
        // Set up parent-child relationship
        new_node->parent = src_current;
        new_node->sibling = src_current->child;
        src_current->child = new_node;
        src_current->has_children = true;
        
        // Insert the new node into the destination track
        if (dest_prev_updated) {
            new_node->next = dest_prev_updated->next;
            dest_prev_updated->next = new_node;
        } else {
            new_node->next = dest_track->head;
            dest_track->head = new_node;
        }
        
        dest_prev_updated = new_node;
        src_current = src_current->next;
    }
    
    // Update the destination track length
    dest_track->length += len;
}