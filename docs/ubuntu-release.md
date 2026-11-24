# Ubuntu 26.04 release

The Ubuntu 26.04.1 LTS x86-64 package includes the camera daemon and its
MediaPipe runtime. The face landmarker model is downloaded separately after
installation; it is not bundled in the package.

For an upgrade, turn Facepalm off and quit the old tray first. Launch the
new tray and turn it on after installation; this also regenerates the user
service unit with its current restart settings.

Download the `.deb` and `SHA256SUMS` from the same release into one folder.
Verify the download, then install the package (replace the filename with the
actual `.deb` name):

```sh
sha256sum -c SHA256SUMS
sudo apt install ./facepalm-package.deb
```

The MCP adapter requires Node.js 22 or later; the package declares that
dependency. Its entry point is `/usr/share/facepalm/mcp/main.js`.

Explicitly fetch and install the pinned model:

```sh
sudo facepalm-install-model
```

The helper downloads the official MediaPipe test asset pinned in
[Linux camera development](linux-camera.md), checks its SHA-256, and installs
it at `/usr/share/facepalm/model/face_landmarker.task`. A failed download,
checksum, or installation leaves any previously installed model in place.
It needs network access and write permission to that location. The package
installation itself does not download the model or access the camera.

For a custom installation prefix, pass `--prefix PATH` to the helper; it puts
the model under `PATH/share/facepalm/model`. After setup, use **Turn On
Facepalm…** in the tray to grant consent and start the service. Model setup
does not start the service or approve a client.

The downloaded model is separate from the package and is not removed by a
package purge. To remove it, run:

```sh
sudo rm /usr/share/facepalm/model/face_landmarker.task
```
