# SEGfault SOUNDboard

## Overview / 项目概述

**SEGfault SOUNDboard** is a C library for audio processing and manipulation, specifically designed for WAV file operations and audio track management. The library provides efficient audio segment handling with support for shared memory segments, cross-correlation-based pattern matching, and flexible audio editing operations.

**SEGfault SOUNDboard** 是一个用于音频处理和操作的 C 语言库，专门设计用于 WAV 文件操作和音频轨道管理。该库提供高效的音频段处理，支持共享内存段、基于交叉相关的模式匹配以及灵活的音频编辑操作。

## Features / 功能特性

- **WAV File I/O**: Load and save WAV files (16-bit PCM, 8000 Hz, mono)
- **Audio Track Management**: Create, read, write, and delete audio segments
- **Shared Memory Segments**: Efficient memory usage through shared audio data
- **Pattern Matching**: Identify advertisements in audio tracks using cross-correlation
- **Audio Insertion**: Insert audio segments with optional shared backing store
- **Linked List Structure**: Flexible audio segment organization

- **WAV 文件 I/O**：加载和保存 WAV 文件（16 位 PCM，8000 Hz，单声道）
- **音频轨道管理**：创建、读取、写入和删除音频段
- **共享内存段**：通过共享音频数据实现高效内存使用
- **模式匹配**：使用交叉相关在音频轨道中识别广告
- **音频插入**：插入音频段，支持可选的共享后备存储
- **链表结构**：灵活的音频段组织方式

## Requirements / 系统要求

- GCC compiler (C99 standard)
- Linux/Unix environment
- Standard C library

- GCC 编译器（C99 标准）
- Linux/Unix 环境
- 标准 C 库

## Building / 编译步骤

### Step 1: Compile the Library

Compile the source code to generate the object file:

```bash
make
```

This will create `sound_seg.o` object file.

### Step 2: Clean Build Artifacts

To remove compiled files:

```bash
make clean
```

### Compilation Flags

The project uses the following compilation flags:
- `-Wall -Wextra`: Enable all warnings
- `-std=c99`: Use C99 standard
- `-fPIC`: Generate position-independent code

## API Documentation / API 文档

### Core Functions / 核心函数

#### Track Management / 轨道管理

```c
// Initialize a new empty audio track
struct sound_seg* tr_init();

// Destroy an audio track and free all resources
void tr_destroy(struct sound_seg* track);

// Get the number of samples in a track
size_t tr_length(struct sound_seg* track);
```

#### Reading and Writing / 读写操作

```c
// Read audio samples from a track
void tr_read(struct sound_seg* track, int16_t* dest, size_t pos, size_t len);

// Write audio samples to a track
void tr_write(struct sound_seg* track, const int16_t* src, size_t pos, size_t len);

// Delete a range of samples from a track
bool tr_delete_range(struct sound_seg* track, size_t pos, size_t len);
```

#### WAV File Operations / WAV 文件操作

```c
// Load raw audio samples from a WAV file
void wav_load(const char* fname, int16_t* dest);

// Save audio samples to a WAV file
void wav_save(const char* fname, const int16_t* src, size_t len);
```

#### Advanced Operations / 高级操作

```c
// Identify advertisement occurrences in a target track
char* tr_identify(const struct sound_seg* target, const struct sound_seg* ad);

// Insert a portion of source track into destination track
void tr_insert(struct sound_seg* src_track, struct sound_seg* dest_track, 
              size_t destpos, size_t srcpos, size_t len);
```

#### Correlation Functions / 相关函数

```c
// Calculate cross-correlation between two audio arrays
double cross_correlation(const int16_t* a, const int16_t* b, size_t len);

// Calculate auto-correlation of an audio array
double auto_correlation(const int16_t* a, size_t len);
```

## Usage Example / 使用示例

### Basic Usage / 基本用法

```c
#include "sound_seg.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    // Create a new audio track
    struct sound_seg* track = tr_init();
    
    // Load audio data from WAV file
    int16_t* buffer = malloc(10000 * sizeof(int16_t));
    wav_load("input.wav", buffer);
    
    // Write audio data to track
    tr_write(track, buffer, 0, 10000);
    
    // Read audio data from track
    int16_t* output = malloc(10000 * sizeof(int16_t));
    tr_read(track, output, 0, 10000);
    
    // Save to WAV file
    wav_save("output.wav", output, 10000);
    
    // Clean up
    free(buffer);
    free(output);
    tr_destroy(track);
    
    return 0;
}
```

### Advertisement Identification / 广告识别

```c
// Load target audio track
struct sound_seg* target = tr_init();
// ... load target audio ...

// Load advertisement audio track
struct sound_seg* ad = tr_init();
// ... load ad audio ...

// Identify ad occurrences
char* result = tr_identify(target, ad);
printf("Ad occurrences: %s\n", result);
free(result);

// Clean up
tr_destroy(target);
tr_destroy(ad);
```

### Audio Insertion / 音频插入

```c
// Create source and destination tracks
struct sound_seg* src = tr_init();
struct sound_seg* dest = tr_init();

// ... load audio data ...

// Insert audio segment (supports shared memory)
tr_insert(src, dest, 1000, 0, 500);

// Clean up
tr_destroy(src);
tr_destroy(dest);
```

## File Structure / 文件结构

```
SEGfault-SOUNDboard/
├── sound_seg.h          # Header file with API declarations
├── sound_seg.c          # Implementation file
├── makefile             # Build configuration
├── .gitignore           # Git ignore rules
└── README.md            # This file
```

## Technical Details / 技术细节

### Audio Format / 音频格式

- **Sample Rate**: 8000 Hz
- **Bit Depth**: 16-bit
- **Channels**: Mono (1 channel)
- **Format**: PCM (uncompressed)

### Data Structure / 数据结构

The library uses a linked list of segment nodes (`seg_node`) to represent audio tracks. Each node can either:
- Store its own audio samples
- Share audio data with another track (for memory efficiency)

### Memory Management / 内存管理

- All tracks must be destroyed using `tr_destroy()` to prevent memory leaks
- Shared segments are automatically handled when writing to shared nodes
- The library uses dynamic memory allocation for audio buffers

## Contributing / 贡献指南

Contributions are welcome! Please ensure that:
- Code follows C99 standard
- All functions are properly documented
- Memory is properly managed (no leaks)
- Code compiles without warnings


## Author / 作者

HusseinP-real

## Repository / 仓库

GitHub: https://github.com/HusseinP-real/SEGfault-SOUNDboard.git

