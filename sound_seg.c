#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>


struct sound_seg {
    //TODO
};

// Load a WAV file into buffer
void wav_load(const char* filename, int16_t* dest){
    // fopen file, read
    FILE* f = fopen(filename, "rb");
    if (!f) {
        return;
    }
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
    return;
}

// Initialize a new sound_seg object
struct sound_seg* tr_init() {
    return NULL;
}

// Destroy a sound_seg object and free all allocated memory
void tr_destroy(struct sound_seg* obj) {
    return;
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    return (size_t)-1;
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    return;
}

// Write len elements from src into position pos
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    return;
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    return true;
}

// Returns a string containing <start>,<end> ad pairs in target
char* tr_identify(struct sound_seg* target, struct sound_seg* ad){
    return NULL;
}

// Insert a portion of src_track into dest_track at position destpos
void tr_insert(struct sound_seg* src_track,
            struct sound_seg* dest_track,
            size_t destpos, size_t srcpos, size_t len) {
    return;
}
