# SD Card Music Player - Voice Command Setup

## Supported Audio Formats

- `.mp3`
- `.wav` (16-bit PCM)
- `.ogg` / `.oga` / `.opus`

## Step 1: Prepare the SD Card

Create a `music` folder on your SD card and copy audio files into it:

```
SD Card root/
  └── music/
      ├── song1.mp3
      ├── song2.mp3
      ├── song3.wav
      └── ...
```

Insert the SD card into the board.

## Step 2: Run menuconfig

```bash
idf.py menuconfig
```

## Step 3: Change settings

### 3a. Switch wake word type

```
Xiaozhi Assistant
  → Wake Word Implementation Type
    → Select: "Multinet model (Custom Wake Word)"
```

### 3b. Set your wake word

These options appear after selecting Custom Wake Word:

```
Xiaozhi Assistant
  → Custom Wake Word: "hi xiao zhi"
  → Custom Wake Word Display: "Hi XiaoZhi"
  → Custom Wake Word Threshold: 20
```

### 3c. Enable an English MultiNet model

This is a **top-level menu** (same level as "Xiaozhi Assistant"):

```
ESP Speech Recognition
  → English Speech Commands Model
    → Select: "general english recognition (mn6_en)"
```

### 3d. Save and exit

Press `S` to save, then `Q` to quit.

## Step 4: Build and flash

```bash
idf.py build && idf.py flash monitor
```

## Step 5: Test voice commands

Once booted, verify in serial monitor:

```
SD card player commands registered (lang=en)
```

### English Commands

| Say this           | Action              |
|--------------------|---------------------|
| `"hi xiao zhi"`    | Wake word (conversation) |
| `"play music"`     | Start playback      |
| `"stop playing"`   | Stop playback       |
| `"next song"`      | Next track          |
| `"previous song"`  | Previous track      |
| `"volume up"`      | Increase volume     |
| `"volume down"`    | Decrease volume     |

### Chinese Commands (if using Chinese MultiNet model)

| Say this               | Action              |
|------------------------|---------------------|
| `"bo fang yin yue"`    | Start playback      |
| `"ting zhi bo fang"`   | Stop playback       |
| `"xia yi shou"`        | Next track          |
| `"shang yi shou"`      | Previous track      |
| `"da sheng yi dian"`   | Increase volume     |
| `"xiao sheng yi dian"` | Decrease volume     |

### French Commands (if using French MultiNet model)

| Say this               | Action              |
|------------------------|---------------------|
| `"jouer musique"`      | Start playback      |
| `"arreter musique"`    | Stop playback       |
| `"chanson suivante"`   | Next track          |
| `"chanson precedente"` | Previous track      |
| `"plus fort"`          | Increase volume     |
| `"moins fort"`         | Decrease volume     |

## Troubleshooting

Check the serial monitor for these log messages:

| Log message | Meaning |
|-------------|---------|
| `SD card mounted at /sdcard` | SD card is working |
| `Found X music files in /sdcard/music` | Music files detected |
| `Registered command: play music` | Voice commands loaded |
| `Music command: play_music (Play music)` | Command recognized |
| `Playing: /sdcard/music/song1.mp3` | Playback started |
| `No music files found` | No supported files in `/sdcard/music/` |
| `Cannot open music directory` | SD card not mounted or wrong path |

## Notes

- Saying the wake word while music is playing will stop the music and start a conversation.
- The language of voice commands is automatically matched to the MultiNet model language configured in the assets.
- Volume changes in steps of 10% (range 0-100).
- Playlist plays in alphabetical order and stops after the last track.
- Next/previous wraps around the playlist.
