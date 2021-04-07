# Apply Patches made to the PJSIP Library

## Applying \*.diff files

```
patch -p1 somefolder/filename.whatever ../patches/patch_file.diff
```

Example: (pwd = lib/pjsip)

```
patch -p1 linux_x86-64/pjproject-2.10/pjmedia/include/pjmedia/stream.h patches/pjmedia_stream_h.diff
patch -p1 linux_x86-64/pjproject-2.10/pjmedia/src/pjmedia/stream.c patches/pjmedia_stream_c.diff
```

## Generating \*.diff files

```
git diff HEAD -- mac_x86-64/pjproject-2.10/pjmedia/src/pjmedia/stream.c > patches/pjmedia_stream_c.diff
```
