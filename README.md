# Lenovo Yoga C630

## Build
```
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j12
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- modules_install INSTALL_MOD_PATH=tmp-modules INSTALL_MOD_STRIP=1

```

## Deploy
Copy the following to your EFI file system:

```
arch/arm64/boot/Image
arch/arm64/boot/dts/qcom/sdm850-lenovo-yoga-c630.dtb
```

Copy the content of `tmp/modules` to your `/lib/modules` once booted.

## Boot
Boot with `efi=novamap` on the command line.
