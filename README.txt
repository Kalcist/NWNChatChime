NWN CHAT CHIME 1.4
Windows 10/11, 64-bit

DOWNLOAD

Ready-to-run Windows 10/11 (64-bit) app:
https://github.com/Kalcist/NWNChatChime/releases/tag/v1.4

Under Assets, download NWNChatChime-v1.4-win64.zip, extract it, and
open NWNChatChime.exe inside the NWNChatChime folder.

GET STARTED

1. Extract this ZIP. Double-click NWNChatChime.exe.
   There is no installer, Python requirement, or administrator requirement.

2. Enable logging inside NWN: open Options and use the search/filter box
   to find "log". Enable Game Log Chat Text (game.log.chat.text.enabled).
   Leave Game Log Chat All (game.log.chat.all.enabled) off to avoid
   duplicate chat and combat output. Apply/save the changes.
   On older EE builds, Ctrl+Shift+F12 opens the debug menu; use its Config
   pane and filter for "log" to find the same options.
   If the logs folder was not found, use Browse to select the actual
   Neverwinter Nights\logs folder.

3. Enter your character's exact name as it appears before messages in the
   chat log. For example: Khalen. This keeps your own messages from ringing.
   Separate multiple character names with semicolons.

4. Click "Test chime", then click the large OFF button to turn alerts ON.
   Have another player speak nearby. Their new chat should appear in the
   "Last matching message" box and play the chime.

ON AND OFF

The app starts OFF every time. The large button toggles alerts.
Ctrl+Alt+F8 also toggles alerts if another app has not reserved that shortcut.
Right-click the bell icon in the Windows tray for the same control.

OFF stops an automatic sound already playing and discards incoming messages.
Turning ON skips old chat instead of playing a backlog. Test chime is an
intentional manual preview and works while OFF.

Minimize keeps the app running in the tray. Double-click its tray icon to
reopen the window. Closing the window or choosing Quit stops the app.

WHAT IT LISTENS FOR

By default: local speech, local roleplay emotes, whispers, and private tells.
Party chat and shouts can be enabled with their checkboxes.

The app catches ordinary local speech even when the log has no [Talk] tag.
It ignores common engine/system lines. Player-chat logging is the preferred
mode because entire-window logging also contains combat and server output.

The log does not identify a silent arrival, exact player distance, or whom
a local message was meant for. Other nearby conversations and NPC dialogue
can also produce alerts. "Only if text contains" can narrow alerts to a
name or phrase, but it will then miss messages that omit that phrase.

SOUND

The original two-note fantasy chime is built into the executable.
Volume is independent of NWN's volume. The default delay is four seconds
between chimes so a conversation does not produce constant ringing.

Choose WAV accepts a short 16-bit PCM WAV, mono or stereo, up to 10 seconds.
Reset sound restores the built-in chime. The original WAV is also included
as fantasy-chime.wav. If a chosen sound becomes unavailable, the app falls
back to the built-in chime.

"Only alert when NWN is in the background" suppresses automatic chimes
while nwmain.exe or nwnmain.exe is the foreground application. It is OFF
by default. The test button still previews the sound.

IF SOMETHING DOES NOT WORK

No chime from Test: check this app's slider, your Windows volume mixer,
and the selected Windows audio output. Reset sound restores the default.

No matching messages: confirm the app is ON, the right channels are checked,
and "Only if text contains" is empty. Watch the status line. If no new chat
is read, verify the correct log folder and enable logging, then restart NWN.
The usual EE folder is Documents\Neverwinter Nights\logs; Windows may
redirect Documents into OneDrive. The app asks Windows for its actual path.

Own messages ring: enter the complete displayed character name, including
any surname. Player/account names can differ from character names.

Alerts can only arrive after NWN writes the text to disk. A customized
server's chat format may require a parser adjustment. The final live check
is another player speaking on your server with the app ON.

Windows publisher notice: this is an unsigned custom executable.

FILES AND SETTINGS

The app reads your local client logs and plays Windows audio. It contains
no network client, game-memory access, DLL injection, or automated gameplay.
It never writes to the chat log. Chat text appears in its preview but is
not saved by the app or sent anywhere.

Preferences are stored at:
%LOCALAPPDATA%\NWNChatChime\settings.ini

Enable logging through NWN's own Options menu as described above.
The app does not edit NWN's game settings or create settings backups.

Setting guidance was checked against the NWNLogRotator project's README
and Beamdog's developer explanation of live chat logging:
https://github.com/voc0der/NWNLogRotator
https://forums.beamdog.com/discussion/80207/nwn-ee-chat-logging

VALIDATION

The app was cross-compiled as a native Windows GUI executable with only
Windows system DLL dependencies. The shared parser, on/off gate, cooldown,
partial-write framing, rotation/reset state, and settings updater were
checked with automated tests and address/undefined-behavior sanitizers.

The supplied Windows integration checks compile, but Windows UI/audio and
a live NWN server could not be exercised in this build environment.

SOURCE

Full source, embedded assets, and test sources are in the source folder.
See source\BUILD.txt to rebuild. You only need the executable to use it.

SYSTEM MESSAGE FILTER (1.4)

Ignore system messages is checked by default. It excludes Loading Screen,
Weather, Area Description, Announcement and the pictured Viscara - Jedi
Temple Exterior speaker. Engine/server/combat and unnamed status lines
are also excluded by the parser. Player dialogue quoting these notices
remains eligible. The app cannot identify every scripted area description
from a screen pane alone; other server-specific speaker names may need
additional rules using their exact log lines.

UPDATING
Close the old app, extract this build and launch the new executable.
Your saved log folder and preferences are retained.

CHAT HISTORY BY CHARACTER (1.4)

Click Chat history, then click character names in the list to select or
deselect multiple people. Their messages appear together in chronological
order, with each speaker identified. No Ctrl key is needed. Click All
characters to follow everyone, including new speakers. The window
updates as new chat arrives and supports selecting/copying text with Ctrl+C.
Includes your own messages, outgoing tells, and channels regardless of the
sound filters. Ignore system messages still applies to collection.

History collects only while ON and holds the latest 10,000 chat messages
across all characters in memory for this app run. Turning OFF pauses
collection and sound; previously collected history remains readable.
It does not import earlier log entries or save chat after quitting.
Closing the history window hides it; Clear history empties it.
