# FileFiles
This utility is a way to declutter your downloads folder or any other folder.
File (move) files in the current directory to appropriate directories.


## Installation
Download and copy the filefiles binary in your path.

## Setup
Create a config file with a list of common subject/archive directories along with a shortcut key.

```bash
touch ~/.config/cleanup.conf
echo "s:/Users/rajpatil/standing/" >> ~/.config/cleanup.conf
echo "m:/Users/rajpatil/Movies/" >> ~/.config/cleanup.conf
echo "p:/Users/rajpatil/Pictures/" >> ~/.config/cleanup.conf
echo "a:/Users/rajpatil/Music/" >> ~/.config/cleanup.conf
echo "w:/Users/"

```
## Usage

1. Navigate to your Downloads folder (or any other directory that you need declutter)
2. Run ``filefiles *`
3. The program will loop through all files and prompt with following for each -
   1. Quit
   2. Quickview - Quickview using MacOS Quickview utility
   2. Move (select from the directories in the cleanup.conf)
   3. PrevDest (reuse the last moved directory)
   4. Custom - Enter a path to move to

### Options

### Examples

## Compiling ##

```bash
zig cc filefiles.c -o filefiles
```
