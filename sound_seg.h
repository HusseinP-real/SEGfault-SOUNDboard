#ifndef SOUND_SEG_H
#define SOUND_SEG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * The sound_seg structure represents an audio track.
 * This is an opaque structure - details are defined in the implementation file.
 */
struct sound_seg;

/**
 * Calculates the cross-correlation between two audio sample arrays.
 * Used to measure similarity between audio segments.
 *
 * @param a First array of audio samples
 * @param b Second array of audio samples
 * @param len Length of both arrays
 * @return The cross-correlation value
 */
double cross_correlation(const int16_t* a, const int16_t* b, size_t len);

/**
 * Calculates the auto-correlation of an audio sample array with itself.
 * Used as a reference value for correlation calculations.
 *
 * @param a Array of audio samples
 * @param len Length of the array
 * @return The auto-correlation value
 */
double auto_correlation(const int16_t* a, size_t len);

/**
 * Loads raw audio samples from a WAV file into a destination buffer.
 * The WAV file header is discarded during loading.
 *
 * @param fname The path to the WAV file
 * @param dest The destination buffer to store audio samples
 */
void wav_load(const char* fname, int16_t* dest);

/**
 * Saves audio samples to a WAV file.
 * Creates a valid WAV file with appropriate header.
 *
 * @param fname The path to the WAV file to create
 * @param src The source buffer containing audio samples
 * @param len The number of samples to save
 */
void wav_save(const char* fname, const int16_t* src, size_t len);

/**
 * Initializes a new empty audio track.
 *
 * @return A pointer to the newly created sound_seg structure
 */
struct sound_seg* tr_init();

/**
 * Destroys an audio track and frees all associated resources.
 *
 * @param track The audio track to destroy
 */
void tr_destroy(struct sound_seg* track);

/**
 * Returns the current number of samples in an audio track.
 *
 * @param track The audio track
 * @return The number of samples in the track
 */
size_t tr_length(struct sound_seg* track);

/**
 * Reads audio samples from a track into a destination buffer.
 *
 * @param track The source audio track
 * @param dest The destination buffer
 * @param pos The starting position in the track
 * @param len The number of samples to read
 */
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len);

/**
 * Writes audio samples from a source buffer into a track.
 * If the write extends beyond the track's length, the track is extended.
 *
 * @param track The destination audio track
 * @param src The source buffer
 * @param pos The position in the track to start writing
 * @param len The number of samples to write
 */
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len);

/**
 * Deletes a range of samples from a track.
 * After deletion, the track's content before and after the deleted range becomes contiguous.
 *
 * @param track The audio track
 * @param pos The starting position of the range to delete
 * @param len The number of samples to delete
 * @return true if deletion was successful, false otherwise
 */
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len);

/**
 * Identifies occurrences of an advertisement within a target track.
 *
 * @param target The target audio track to search in
 * @param ad The advertisement audio track to search for
 * @return A dynamically allocated string containing start,end pairs of ad occurrences
 */
char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad);

/**
 * Inserts a portion of a source track into a destination track.
 * The inserted portion shares backing store with the source.
 *
 * @param src_track The source audio track
 * @param dest_track The destination audio track
 * @param destpos The position in the destination track to insert at
 * @param srcpos The starting position in the source track
 * @param len The number of samples to insert
 */
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, 
              size_t destpos, size_t srcpos, size_t len);

#endif /* SOUND_SEG_H */