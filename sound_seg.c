#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>


struct sound_seg {
    // //TODO
    uint16_t* samples; // array of samples
    size_t capacity; // judge if the array is full
    size_t length; // length of the array
};

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
    fwrite("fmt", 1, 4, f);
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

// Initialize a new sound_seg object
struct sound_seg* tr_init() {
    struct sound_seg* track = (struct sound_seg*)malloc(sizeof(struct sound_seg));
    // allocate memory failed
    if (!track) return NULL;

    // initialize
    track->samples = NULL;
    track->capacity = 0;
    return track;
}

// Destroy a sound_seg object and free all allocated memory
void tr_destroy(struct sound_seg* obj) {
    // if the pointer is null return
    if (!obj) return;

    // free the memory if its not null
    if (obj->samples) {
        free(obj->samples);
        obj->samples = NULL;
    }

    free(obj);

    return;
}

// Return the length of the segment
size_t tr_length(struct sound_seg* seg) {
    if (!seg) return 0;
    return seg->length;
    //return (size_t)-1;
}

// Read len elements from position pos into dest
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len) {
    if (!track || !track->samples || !dest) return;
    if (pos >= track->length) return;
    size_t can_read = track->length - pos;
    if (len > can_read) len = can_read;
    memcpy(dest, track->samples + pos, len * sizeof(int16_t));
    return;
}

// Write len elements from src into position pos
void tr_write(struct sound_seg* track, int16_t* src, size_t pos, size_t len) {
    if (!track || !src) return;

    // if pose is greater than length, set pos as the end of the track
    if (pos > track->length) pos = track->length;
    size_t end_pos = pos + len;

    // if end_pos is greater than capacity, reallocate memory
    if (end_pos > track-> capacity) {
        size_t new_capacity = end_pos;
        int16_t* new_samples = (int16_t*)realloc(track->samples, new_capacity * sizeof(int16_t));
        if (!new_samples) return;

        // update the pointer and capacity
        track->samples = new_samples;
        track->capacity = new_capacity;
    }
    memcpy(track->samples + pos, src, len * sizeof(int16_t));

    // update the length
    if (end_pos > track->length) track->length = end_pos;

    return;
}

// Delete a range of elements from the track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len) {
    if (!track || !track->samples) return false;
    if (pos >= track->length) return false;
    size_t available = track->length - pos;
    if (len > available) len = available;

    size_t remain = track->length - pos - len;
    memmove(track->samples + pos, 
        track->samples + pos + len, 
        remain * sizeof(int16_t));

    track->length -= len;
    
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
