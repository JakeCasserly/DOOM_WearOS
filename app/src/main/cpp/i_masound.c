//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2008 David Flater
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//  System interface for sound.
//

#include <stdio.h>
#include <stdlib.h>
#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_AAUDIO
// Add this line to enable OGG Vorbis support:
#define MA_NO_MP3
#define MA_NO_FLAC
// DON'T add MA_NO_VORBIS - we need Vorbis for OGG

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"  // From miniaudio extras

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_RESOURCE_MANAGER
#define MA_ENABLE_VORBIS  // Enable OGG Vorbis
#include "miniaudio.h"

/* 3. Now implement stb_vorbis by UNDEFINING the header-only flag */
#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include "i_sound.h"
#include "i_system.h"
#include "i_swap.h"
#include "m_misc.h"
#include "w_wad.h"
#include "z_zone.h"

#include "doomtype.h"

#define ARRLEN(x) (sizeof(x) / sizeof(*(x)))

#define NUM_CHANNELS 16

typedef struct allocated_sound_s allocated_sound_t;

struct allocated_sound_s {
    ma_sound *sound;
    ma_audio_buffer *audio_buffer; //needed for lifetime
    allocated_sound_t *prev, *next;
};

static boolean sound_initialized = false;

static sfxinfo_t *channels_playing[NUM_CHANNELS];

static boolean use_sfx_prefix = true;

static boolean use_music_prefix = true;

typedef struct {
    // If this is NULL, the file cannot be mapped into memory.  If this
    // is non-NULL, it is a pointer to the mapped file.
    byte *mapped;

    // Length of the file, in bytes.
    unsigned int length;
} music_file_t;

typedef struct {
    music_file_t music;
    FILE *fstream;
} stdc_music_file_t;

music_file_t *MA_OpenFile(const char *path)
{
    stdc_music_file_t *result;
    FILE *fstream = NULL;

    #ifdef __ANDROID__
    #include "AndroidDriver.h"
        fstream = android_fopen(path, "rb");
    #else
        fstream = fopen(path, "rb");
    #endif

    if (fstream == NULL)
        return NULL;

    // Create a new stdc_music_file_t to hold the file handle.

    result = Z_Malloc(sizeof(stdc_music_file_t), PU_STATIC, 0);
    result->music.mapped = NULL;
    result->music.length = M_FileLength(fstream);
    result->fstream = fstream;

    return &result->music;
}

static ma_engine engine;

// Doubly-linked list of allocated sounds.
// When a sound is played, it is moved to the head, so that the oldest
// sounds not used recently are at the tail.
static allocated_sound_t *allocated_sounds_head = NULL;
static allocated_sound_t *allocated_sounds_tail = NULL;
static size_t allocated_sounds_size = 0;

// External reference to Android app (from doomgeneric_android.c)
//extern struct android_app *gapp;

// Hook a sound into the linked list at the head.
static void AllocatedSoundLink(allocated_sound_t *snd)
{
    snd->prev = NULL;

    snd->next = allocated_sounds_head;
    allocated_sounds_head = snd;

    if (allocated_sounds_tail == NULL)
        allocated_sounds_tail = snd;
    else
        snd->next->prev = snd;
}

// Allocate a block for a new sound effect.
static void AllocateSound(sfxinfo_t *sfxinfo, byte *data, size_t len, int sample_rate)
{
    // Allocate the sound structure and data.  The data will immediately
    // follow the structure, which acts as a header.
    allocated_sound_t *snd = malloc(sizeof(allocated_sound_t));
    if (snd == NULL)
        return;

    ma_audio_buffer_config buffer_config = ma_audio_buffer_config_init(
            ma_format_u8,
            1,
            len/2,
            data,
            NULL);
    buffer_config.sampleRate = sample_rate;

    ma_audio_buffer *audio_buffer = malloc(sizeof(ma_audio_buffer));
    if (ma_audio_buffer_init(&buffer_config, audio_buffer) != MA_SUCCESS)
    {
        free(snd);
        return;
    }

    ma_sound *sound = malloc(sizeof(ma_sound));
    ma_result result = ma_sound_init_from_data_source(&engine, audio_buffer, MA_SOUND_FLAG_DECODE, NULL, sound);
    if (result != MA_SUCCESS)
    {
        free(snd);
        return;
    }

    snd->sound = sound;
    snd->audio_buffer = audio_buffer;

    // driver_data pointer points to the allocated_sound structure.
    sfxinfo->driver_data = snd;

    // Keep track of how much memory all these cached sounds are using...
    allocated_sounds_size += len;

    AllocatedSoundLink(snd);
}

// When a sound stops, check if it is still playing.  If it is not,
// we can mark the sound data as CACHE to be freed back for other
// means.
static void ReleaseSoundOnChannel(int channel)
{
    sfxinfo_t *sfxinfo = channels_playing[channel];
    if (sfxinfo == NULL)
        return;

    channels_playing[channel] = NULL;
}

static void GetSfxLumpName(sfxinfo_t *sfx, char *buf, size_t buf_len)
{
    // Linked sfx lumps? Get the lump number for the sound linked to.
    if (sfx->link != NULL)
        sfx = sfx->link;

    // Doom adds a DS* prefix to sound lumps; Heretic and Hexen don't do this.
    if (use_sfx_prefix)
        M_snprintf(buf, buf_len, "ds%s", sfx->name);
    else
        M_StringCopy(buf, sfx->name, buf_len);
}

static void I_MA_PrecacheSounds(sfxinfo_t *sounds, int num_sounds)
{
    for (int i = 0; i < num_sounds; ++i)
    {
        sfxinfo_t *sfxinfo = &sounds[i];

        // need to load the sound
        char name[9];
        GetSfxLumpName(sfxinfo, name, sizeof(name));
        sfxinfo->lumpnum = W_CheckNumForName(name);
        //printf("sfx->name = %s", sfxinfo->name);
        //printf("sfx->lumpnum = %d", sfxinfo->lumpnum);
        int lumpnum = sfxinfo->lumpnum;
        if (lumpnum == -1)
            continue;

        byte *data = W_CacheLumpNum(lumpnum, PU_STATIC);
        unsigned int lumplen = W_LumpLength(lumpnum);

        // Check the header, and ensure this is a valid sound
        if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00)
            continue;

        // 16 bit sample rate field, 32 bit length field
        int samplerate = (data[3] << 8) | data[2];
        unsigned int length = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];

        // If the header specifies that the length of the sound is greater than
        // the length of the lump itself, this is an invalid sound lump

        // We also discard sound lumps that are less than 49 samples long,
        // as this is how DMX behaves - although the actual cut-off length
        // seems to vary slightly depending on the sample rate.  This needs
        // further investigation to better understand the correct behavior.
        if (length > lumplen - 8 || length <= 48)
            continue;

        // The DMX sound library seems to skip the first 16 and last 16
        // bytes of the lump - reason unknown.
        data += 16;
        length -= 32;

        // Sample rate conversion
        AllocateSound(sfxinfo, data, length, samplerate);

        // don't need the original lump any more
        //W_ReleaseLumpNum(lumpnum);
    }
}

static void I_MA_UpdateSoundParams(int handle, int vol, int sep)
{
    if (!sound_initialized || handle < 0 || handle >= NUM_CHANNELS)
        return;

    int left = ((254 - sep) * vol) / 127;
    int right = ((sep) * vol) / 127;

    if (left < 0)
        left = 0;
    else if (left > 255)
        left = 255;

    if (right < 0)
        right = 0;
    else if (right > 255)
        right = 255;

    sfxinfo_t *sfxinfo = channels_playing[handle];
    if (sfxinfo == NULL)
        return;

    allocated_sound_t *snd = sfxinfo->driver_data;
    if (snd == NULL)
        return;

    ma_sound_set_volume(snd->sound, (float)vol/64.0f);
    ma_sound_set_pan(snd->sound, (float)(right-left));
}

// Retrieve the raw data lump index for a given SFX name.
static int I_MA_GetSfxLumpNum(sfxinfo_t *sfx)
{
    char name[9];
    GetSfxLumpName(sfx, name, sizeof(name));

    return W_GetNumForName(name);
}

// Starting a sound means adding it
//  to the current list of active sounds
//  in the internal channels.
// As the SFX info struct contains
//  e.g. a pointer to the raw data,
//  it is ignored.
// As our sound handling does not handle
//  priority, it is ignored.
// Pitching (that is, increased speed of playback)
//  is set, but currently not used by mixing.
static int I_MA_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep)
{
    if (!sound_initialized || channel < 0 || channel >= NUM_CHANNELS)
        return -1;

    // Release a sound effect if there is already one playing
    // on this channel
    ReleaseSoundOnChannel(channel);

    allocated_sound_t *snd = sfxinfo->driver_data;
    if (snd == NULL)
        return -1;

    // set separation, etc.
    I_MA_UpdateSoundParams(channel, vol, sep);

    // play sound
    ma_sound_start(snd->sound);

    channels_playing[channel] = sfxinfo;

    return channel;
}

static void I_MA_StopSound(int handle)
{
    if (!sound_initialized || handle < 0 || handle >= NUM_CHANNELS)
        return;

    sfxinfo_t *sfxinfo = channels_playing[handle];
    if (sfxinfo == NULL)
        return;

    allocated_sound_t *snd = sfxinfo->driver_data;
    if (snd == NULL)
        return;

    ma_sound_stop(snd->sound);

    // Sound data is no longer needed; release the
    // sound data being used for this channel
    ReleaseSoundOnChannel(handle);
}

static boolean I_MA_SoundIsPlaying(int handle)
{
    if (!sound_initialized || handle < 0 || handle >= NUM_CHANNELS)
        return false;

    sfxinfo_t *sfxinfo = channels_playing[handle];
    if (sfxinfo == NULL)
        return false;

    allocated_sound_t *snd = sfxinfo->driver_data;
    if (snd == NULL)
        return false;

    return ma_sound_is_playing(snd->sound);
}

// Periodically called to update the sound system
static void I_MA_UpdateSound(void)
{
    // Check all channels to see if a sound has finished
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        if (channels_playing[i] && !I_MA_SoundIsPlaying(i))
        {
            // Sound has finished playing on this channel,
            // but sound data has not been released to cache
            ReleaseSoundOnChannel(i);
        }
    }
}

static void I_MA_ShutdownSound(void)
{
    if (!sound_initialized)
        return;

    ma_engine_uninit(&engine);

    sound_initialized = false;
}

static boolean I_MA_InitSound(boolean _use_sfx_prefix)
{
    use_sfx_prefix = _use_sfx_prefix;

    // No sounds yet
    for (int i = 0; i < NUM_CHANNELS; ++i)
        channels_playing[i] = NULL;

    if (ma_engine_init(NULL, &engine) != MA_SUCCESS) {
        printf("Failed init audio engine.\n");
        return false;
    }

    sound_initialized = true;

    return true;
}

static snddevice_t sound_ma_devices[] = {
        SNDDEVICE_SB,
        SNDDEVICE_PAS,
        SNDDEVICE_GUS,
        SNDDEVICE_WAVEBLASTER,
        SNDDEVICE_SOUNDCANVAS,
        SNDDEVICE_AWE32
};

sound_module_t sound_ma_module = {
        sound_ma_devices,
        ARRLEN(sound_ma_devices),
        I_MA_InitSound,
        I_MA_ShutdownSound,
        I_MA_GetSfxLumpNum,
        I_MA_UpdateSound,
        I_MA_UpdateSoundParams,
        I_MA_StartSound,
        I_MA_StopSound,
        I_MA_SoundIsPlaying,
        I_MA_PrecacheSounds
};

// Simple MP3/OGG Music Implementation for Doom
// This replaces the MIDI/MUS system with external music files



// Music state
static ma_sound *current_music_sound = NULL;
static void *current_music_buffer = NULL;
static int current_music_buffer_size = 0;

// Mapping of Doom music lump names to filenames
// D_E1M1 = Episode 1, Map 1, etc.
// Files are in assets/ root folder
static const char *music_files[] = {
        "D_E1M1", "d_e1m1.ogg",
        "D_E1M2", "d_e1m2.ogg",
        "D_E1M3", "d_e1m3.ogg",
        "D_E1M4", "d_e1m4.ogg",
        "D_E1M5", "d_e1m5.ogg",
        "D_E1M6", "d_e1m6.ogg",
        "D_E1M7", "d_e1m7.ogg",
        "D_E1M8", "d_e1m8.ogg",
        "D_E1M9", "d_e1m9.ogg",
        "D_E2M1", "d_e2m1.ogg",
        "D_E2M2", "d_e2m2.ogg",
        "D_E2M3", "d_e2m3.ogg",
        "D_E2M4", "d_e2m4.ogg",
        "D_E2M5", "d_e2m5.ogg",
        "D_E2M6", "d_e2m6.ogg",
        "D_E2M7", "d_e2m7.ogg",
        "D_E2M8", "d_e2m8.ogg",
        "D_E2M9", "d_e2m9.ogg",
        "D_E3M1", "d_e3m1.ogg",
        "D_E3M2", "d_e3m2.ogg",
        "D_E3M3", "d_e3m3.ogg",
        "D_E3M4", "d_e3m4.ogg",
        "D_E3M5", "d_e3m5.ogg",
        "D_E3M6", "d_e3m6.ogg",
        "D_E3M7", "d_e3m7.ogg",
        "D_E3M8", "d_e3m8.ogg",
        "D_E3M9", "d_e3m9.ogg",
        "D_INTER", "d_inter.ogg",  // Intermission
        "D_INTRO", "d_intro.ogg",  // Introduction
        "D_BUNNY", "d_bunny.ogg",  // Ending
        "D_VICTOR", "d_victor.ogg", // Victory
        "D_INTROA", "d_introa.ogg", // Doom 2 intro
        "D_RUNNIN", "d_runnin.ogg", // Doom 2 maps...
        "D_STALKS", "d_stalks.ogg",
        "D_COUNTD", "d_countd.ogg",
        "D_BETWEE", "d_betwee.ogg",
        "D_DOOM", "d_doom.ogg",
        "D_THE_DA", "d_the_da.ogg",
        "D_SHAWN", "d_shawn.ogg",
        "D_DDTBLU", "d_ddtblu.ogg",
        "D_IN_CIT", "d_in_cit.ogg",
        "D_DEAD", "d_dead.ogg",
        "D_STLKS2", "d_stlks2.ogg",
        "D_THEDA2", "d_theda2.ogg",
        "D_DOOM2", "d_doom2.ogg",
        "D_DDTBL2", "d_ddtbl2.ogg",
        "D_RUNNI2", "d_runni2.ogg",
        "D_DEAD2", "d_dead2.ogg",
        "D_STLKS3", "d_stlks3.ogg",
        "D_ROMERO", "d_romero.ogg",
        "D_SHAWN2", "d_shawn2.ogg",
        "D_MESSAG", "d_messag.ogg",
        "D_COUNT2", "d_count2.ogg",
        "D_DDTBL3", "d_ddtbl3.ogg",
        "D_AMPIE", "d_ampie.ogg",
        "D_THEDA3", "d_theda3.ogg",
        "D_ADRIAN", "d_adrian.ogg",
        "D_MESSG2", "d_messg2.ogg",
        "D_ROMER2", "d_romer2.ogg",
        "D_TENSE", "d_tense.ogg",
        "D_SHAWN3", "d_shawn3.ogg",
        "D_OPENIN", "d_openin.ogg",
        "D_EVIL", "d_evil.ogg",
        "D_ULTIMA", "d_ultima.ogg",
        "D_READ_M", "d_read_m.ogg",
        "D_DM2TTL", "d_dm2ttl.ogg",
        "D_DM2INT", "d_dm2int.ogg",
        NULL, NULL  // Terminator
};

extern AAssetManager* GetAssetManager(void);

// Load music file from Android assets
static void *LoadMusicFromAssets(const char *filepath, int *out_size)
{
    AAssetManager *assetManager = GetAssetManager();
    if (!assetManager)
    {
        printf("ERROR: Asset manager not available!\n");
        return NULL;
    }

    AAsset *asset = AAssetManager_open(assetManager, filepath, AASSET_MODE_BUFFER);
    if (!asset)
    {
        printf("ERROR: Could not open music file: %s\n", filepath);
        return NULL;
    }

    int size = AAsset_getLength(asset);
    void *buffer = malloc(size);

    if (!buffer)
    {
        printf("ERROR: Failed to allocate buffer for music\n");
        AAsset_close(asset);
        return NULL;
    }

    if (AAsset_read(asset, buffer, size) < 0)
    {
        printf("ERROR: Failed to read music file\n");
        free(buffer);
        AAsset_close(asset);
        return NULL;
    }

    AAsset_close(asset);

    *out_size = size;
    printf("Loaded music file: %s (%d bytes)\n", filepath, size);
    return buffer;
}

// Find music file path from lump name
static const char *GetMusicFilePath(const char *lump_name)
{
    for (int i = 0; music_files[i] != NULL; i += 2)
    {
        if (strcasecmp(music_files[i], lump_name) == 0)
        {
            return music_files[i + 1];
        }
    }
    return NULL;
}



// Music-specific globals
static ma_sound *current_music = NULL;
static ma_decoder *current_decoder = NULL;
static void *current_music_data = NULL;
static boolean music_initialized = false;
static int music_volume = 100;

// Initialize music subsystem
static boolean I_MA_InitMusic(void)
{
    // Engine should already be initialized by I_MA_InitSound
    // If not, initialize it here
    if (!sound_initialized)
    {
        if (ma_engine_init(NULL, &engine) != MA_SUCCESS)
        {
            printf("Failed to init audio engine for music.\n");
            return false;
        }
        sound_initialized = true;
    }

    printf("Initializing music!");

    music_initialized = true;
    return true;
}

// Stop music
static void I_MA_StopMusic(void)
{
    if (current_music != NULL)
    {
        ma_sound_stop(current_music);
    }
}

// Shutdown music subsystem
static void I_MA_ShutdownMusic(void)
{
    if (!music_initialized)
        return;

    I_MA_StopMusic();
    music_initialized = false;
}

// Set music volume (0-127)
static void I_MA_SetMusicVolume(int volume)
{
    music_volume = volume;

    if (current_music != NULL)
    {
        // Convert 0-127 range to 0.0-1.0
        float vol = (float)volume / 127.0f;
        ma_sound_set_volume(current_music, vol);
    }
}

// Pause music
static void I_MA_PauseMusic(void)
{
    if (current_music != NULL)
    {
        ma_sound_stop(current_music);
    }
}

// Resume music
static void I_MA_ResumeMusic(void)
{
    if (current_music != NULL)
    {
        ma_sound_start(current_music);
    }
}

// Unregister song
static void I_MA_UnRegisterSong(void *handle)
{
    if (handle == NULL)
        return;

    printf("Song being unregistered!");

    ma_sound *sound = (ma_sound *)handle;

    // Stop if playing
    ma_sound_stop(sound);

    printf("Sound stopped");

    // Cleanup

    ma_sound_uninit(sound);

    free(sound);

    printf("Memory freed!");

    if (current_decoder != NULL)
    {
        ma_decoder_uninit(current_decoder);
        free(current_decoder);
        current_decoder = NULL;
    }

    if (current_music_data != NULL)
    {
        free(current_music_data);
        current_music_data = NULL;
    }

    current_music = NULL;
}

// Check if data is MUS format
static boolean IsMUSFormat(void *data, int len)
{
    if (len < 4)
        return false;

    unsigned char *bytes = (unsigned char *)data;
    return (bytes[0] == 'M' && bytes[1] == 'U' && bytes[2] == 'S' && bytes[3] == 0x1A);
}

// Register song - now loads from external files instead of WAD data
static void *I_MA_RegisterSong(void *data, int len)
{
    printf("RegisterSong called\n");

    if (!music_initialized)
    {
        printf("ERROR: Music not initialized!\n");
        return NULL;
    }

    // Clean up any existing music
    if (current_music_sound != NULL)
    {
        I_MA_UnRegisterSong(current_music_sound);
    }

    // Try to determine the music lump name from the data
    // The calling code should pass the musicinfo_t->name
    // For now, we'll use a workaround - check if this is MUS/MIDI format

    if (IsMUSFormat(data, len))
    {
        // MUS format detected - we need to map this to an external file
        // Since we don't have the lump name here, we'll need to modify
        // the calling code to pass it. For now, log a warning.
        printf("WARNING: MUS format detected but external music files should be used\n");
        printf("You need to modify the calling code to use musicinfo_t->name\n");
        return NULL;
    }

    // If data looks like it could be MP3/OGG already, try to use it
    // Check for OGG header: "OggS"
    unsigned char *bytes = (unsigned char *)data;
    boolean is_ogg = (len >= 4 && bytes[0] == 'O' && bytes[1] == 'g' &&
                      bytes[2] == 'g' && bytes[3] == 'S');

    // Check for MP3 header: 0xFF 0xFB or ID3
    boolean is_mp3 = (len >= 3 && ((bytes[0] == 0xFF && (bytes[1] & 0xE0) == 0xE0) ||
                                   (bytes[0] == 'I' && bytes[1] == 'D' && bytes[2] == '3')));

    void *music_data = NULL;
    int music_size = 0;

    if (is_ogg || is_mp3)
    {
        // Use the provided data directly
        music_data = malloc(len);
        if (!music_data)
        {
            printf("ERROR: Failed to allocate music buffer\n");
            return NULL;
        }
        memcpy(music_data, data, len);
        music_size = len;
        printf("Using provided %s data (%d bytes)\n", is_ogg ? "OGG" : "MP3", len);
    }
    else
    {
        printf("WARNING: Unknown music format, cannot play\n");
        return NULL;
    }

    // Create decoder from memory
    ma_decoder *decoder = malloc(sizeof(ma_decoder));
    if (!decoder)
    {
        printf("ERROR: Failed to allocate decoder\n");
        free(music_data);
        return NULL;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 44100);
    ma_result result = ma_decoder_init_memory(music_data, music_size, &config, decoder);

    if (result != MA_SUCCESS)
    {
        printf("ERROR: Failed to init decoder! Result: %d\n", result);
        free(decoder);
        free(music_data);
        return NULL;
    }

    // Create sound from decoder
    ma_sound *sound = malloc(sizeof(ma_sound));
    if (!sound)
    {
        printf("ERROR: Failed to allocate sound\n");
        ma_decoder_uninit(decoder);
        free(decoder);
        free(music_data);
        return NULL;
    }

    result = ma_sound_init_from_data_source(&engine, decoder, 0, NULL, sound);
    if (result != MA_SUCCESS)
    {
        printf("ERROR: Failed to init sound! Result: %d\n", result);
        free(sound);
        ma_decoder_uninit(decoder);
        free(decoder);
        free(music_data);
        return NULL;
    }

    // Store references
    current_music_sound = sound;
    current_music_buffer = music_data;
    current_music_buffer_size = music_size;

    // Set volume
    I_MA_SetMusicVolume(music_volume);

    printf("Music registered successfully\n");
    return sound;
}

// Alternative: Register song by name (better approach)
static void *I_MA_RegisterSongByName(const char *lump_name)
{
    printf("RegisterSongByName: %s\n", lump_name);

    if (!music_initialized)
    {
        printf("ERROR: Music not initialized!\n");
        return NULL;
    }

    // Clean up existing music
    if (current_music != NULL)
    {
        printf("Unregistering a song\n");
        I_MA_UnRegisterSong(current_music_sound);
    }

//    if (current_music != NULL) {
//        ma_sound_stop(current_music);
//        ma_sound_uninit(current_music);
//        // Important: Do not free the memory until uninit is done
//    }

    // Find the music file path
    const char *filepath = GetMusicFilePath(lump_name);
    if (!filepath)
    {
        printf("WARNING: No music file mapped for %s\n", lump_name);
        return NULL;
    }

    // Load from assets
    int music_size = 0;
    void *music_data = LoadMusicFromAssets(filepath, &music_size);
    if (!music_data)
    {
        printf("ERROR: Failed to load music from assets\n");
        return NULL;
    }

    printf("loaded music from assets!\n");

    // Create decoder
    ma_decoder *decoder = malloc(sizeof(ma_decoder));
    if (!decoder)
    {
        free(music_data);
        return NULL;
    }

    // Let miniaudio auto-detect the format (OGG, MP3, WAV, etc.)
    ma_decoder_config config = ma_decoder_config_init_default();
    ma_result result = ma_decoder_init_memory(music_data, music_size, &config, decoder);

//    float buffer[1024];
//    ma_uint64 framesRead;
//    ma_decoder_read_pcm_frames(decoder, buffer, 1024, &framesRead);
//    printf("Frames actually read: %llu\n", framesRead);

    if (result != MA_SUCCESS)
    {
        printf("ERROR: Failed to decode music file (error code: %d)\n", result);
        printf("Trying with explicit format detection...\n");

        // Try with explicit format
        config = ma_decoder_config_init(ma_format_f32, 2, 44100);
        result = ma_decoder_init_memory(music_data, music_size, &config, decoder);

        if (result != MA_SUCCESS)
        {
            printf("ERROR: Still failed to decode. Error: %d\n", result);
            free(decoder);
            free(music_data);
            return NULL;
        }
    }

    // Create sound
    ma_sound *sound = malloc(sizeof(ma_sound));
    if (!sound)
    {
        ma_decoder_uninit(decoder);
        free(decoder);
        free(music_data);
        return NULL;
    }

    result = ma_sound_init_from_data_source(&engine, decoder, 0, NULL, sound);
    if (result != MA_SUCCESS)
    {
        printf("ERROR: Failed to create sound from decoder\n");
        free(sound);
        ma_decoder_uninit(decoder);
        free(decoder);
        free(music_data);
        return NULL;
    }

    // Store references
    current_music_sound = sound;
    current_music_buffer = music_data;
    current_music_buffer_size = music_size;

    I_MA_SetMusicVolume(music_volume);

    printf("Music loaded: %s\n", filepath);
    return sound;
}

// Play song
static void I_MA_PlaySong(void *handle, boolean looping)
{
    printf("PlaySong function entered with handle=%p, looping=%d\n", handle, looping);

    if (handle == NULL)
    {
        printf("ERROR: handle is NULL!\n");
        return;
    }

    if (!music_initialized)
    {
        printf("ERROR: Music not initialized!\n");
        return;
    }

    printf("Trying to play song!\n");

    ma_sound *sound = (ma_sound *)handle;

    // Set looping
    ma_sound_set_looping(sound, looping ? MA_TRUE : MA_FALSE);
    printf("Looping set to: %d\n", looping);

    // Start playback
    ma_result result = ma_sound_start(sound);
    if (result != MA_SUCCESS)
    {
        printf("ERROR: Failed to start sound! Result: %d\n", result);
        return;
    }

    printf("Song started successfully!\n");
}

// Poll music (called periodically)
static void I_MA_PollMusic(void)
{
    // miniaudio handles everything internally, no polling needed
    // But we could check if music has finished here if needed
}

// Music module definition
music_module_t music_ma_module = {
        sound_ma_devices,
        ARRLEN(sound_ma_devices),
        I_MA_InitMusic,
        I_MA_ShutdownMusic,
        I_MA_SetMusicVolume,
        I_MA_PauseMusic,
        I_MA_ResumeMusic,
        I_MA_RegisterSong,
        I_MA_RegisterSongByName,
        I_MA_UnRegisterSong,
        I_MA_PlaySong,
        I_MA_StopMusic,
        I_MA_PollMusic
};
