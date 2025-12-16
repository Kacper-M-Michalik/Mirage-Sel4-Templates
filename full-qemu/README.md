This example shows how you can implement a proper VMM that interacts with sel4-space device drivers, as to service hypercalls. Device drivers used are timer, serial and block.
<br>

The SDDF project is used for driver/protocol implementation, additional details about SDDF can be found here:
- https://github.com/au-ts/sddf/tree/main
- https://trustworthy.systems/projects/drivers/sddf-design.pdf

The SDDF system results in complicated and fragile system descriptions, as such the sdfgen python library is used, which allows writing python to help automate generation of a XML system description, as seen in system_gen.py
<br>

Library Versions required:

- sdfgen 0.26.2
- SDDF 0.6.0

<br>
This example is buildable for:

- qemu_virt_aarch64

<br>
Some precompiled MirageOS unikernels can be found in '/example-kernels' folder
<br>

To build this example, fill in and run:
```bash
make MICROKIT_SDK=</your/path/microkit-sdk-2.0.1> MICROKIT_BOARD=<board> MICROKIT_CONFIG=<debug/release/benchmark>
```
You can also specify a specific GUEST_FILE, paths can be local or absolute.

This example can be simulated on QEMU, either run QEMU after building for qemu_virt_aarch64, or add 'qemu' to your build command:
```bash
make qemu MICROKIT_SDK=</your/path/microkit-sdk-2.0.1> MICROKIT_BOARD=<board> MICROKIT_CONFIG=<debug/release/benchmark>
```

The resulting build files and final image (loader.img) can be seen in the 'build/' subfolder.