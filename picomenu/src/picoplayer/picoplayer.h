/*
 * PicoStation 2.0 - PicoPlayer SD (PU-20 Audio Player + SoundScope)
 *
 * Evolved PU-20 player UI designed to browse and play audio files inside
 * SD:/PicoPlayer/ (MP3, WAV, FLAC, OGG, XA/VAG, CD-DA) with 3D PU-20 spheres
 * ahead of each track name, ID3/RIFF metadata tags inside the PU-20 info box,
 * queue programming (PROGRAMAR) and the 12-mode SoundScope.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PP_FMT_CDDA = 0,
    PP_FMT_MP3,
    PP_FMT_WAV,
    PP_FMT_FLAC,
    PP_FMT_OGG,
    PP_FMT_XA,
    PP_FMT_DIR
} PpAudioFormat;

typedef struct {
    char          title[36];     /* ID3/RIFF title or filename without ext  */
    char          artist[32];    /* ID3/RIFF artist tag (optional)          */
    char          album[32];     /* ID3/RIFF album tag (optional)           */
    uint8_t       format;        /* PpAudioFormat                           */
    uint16_t      bitrateKbps;   /* e.g. 320, 192, 1411                     */
    uint16_t      sampleRateHz;  /* e.g. 44100, 37800, 22050                */
    uint8_t       trackNum;      /* underlying CD-DA track or SD entry idx  */
} PpTrackMeta;

/*
 * Enters the player. Reads the audio catalogue (CD-DA TOC or SD:/PicoPlayer/
 * listing once wired to PicoStation RP2040 commands) and resets UI state.
 */
void picoplayer_enter(void);

/*
 * Advances one video frame, handles input and appends the whole PU-20 UI to
 * `chain`. `buttons`/`prevButtons` are raw active-high getButtons() values.
 * Returns true when the user asked to go back to the PicoMenu.
 */
bool picoplayer_update_and_draw(DMAChain *chain, const TextureInfo *font,
                                uint16_t buttons, uint16_t prevButtons);

/*
 * Presents the completed SoundScope front buffer inside VBlank (immediately
 * after waitForVblank() and before sendLinkedList()) for tear-free 320x240p.
 */
void picoplayer_present_vblank(void);

/*
 * Two-phase exit so PicoMenu can show a "PicoMenu / Aguarde, carregando..."
 * modal while the RP2040 unmounts the virtual Audio CD and reloads the TOC.
 */
void picoplayer_begin_exit(void);
void picoplayer_finish_exit(void);

/*
 * Stops playback, mutes CD-DA/I2S capture and restores 320x240p cleanly.
 * Always call this after update_and_draw() returned true.
 */
void picoplayer_exit(void);

#ifdef __cplusplus
}
#endif
