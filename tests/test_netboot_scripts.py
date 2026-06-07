import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def run_script(name: str, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    script_env = os.environ.copy()
    if env:
        script_env.update(env)
    return subprocess.run(
        [str(ROOT / name), *args],
        cwd=ROOT,
        env=script_env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )


def test_config_names_kernel8_img_explicitly() -> None:
    config = (ROOT / "config.txt").read_text()

    assert "\nkernel=kernel8.img\n" in f"\n{config}\n"


def test_eeprom_netboot_config_matches_direct_bench_defaults() -> None:
    config = (ROOT / "netboot-eeprom-config.txt").read_text()

    for line in (
        "BOOT_UART=1",
        "BOOT_ORDER=0xf12",
        "TFTP_FILE_TIMEOUT=30000",
        "TFTP_IP=10.42.0.1",
        "CLIENT_IP=10.42.0.2",
        "SUBNET=255.255.255.0",
        "TFTP_PREFIX=1",
        "TFTP_PREFIX_STR=aether/",
    ):
        assert line in config


def test_prepare_tftp_seeds_prefixed_tree_from_boot_partition(tmp_path: pathlib.Path) -> None:
    boot = tmp_path / "bootfs"
    root = tmp_path / "tftp"
    boot.mkdir()
    (boot / "start4.elf").write_text("firmware")
    (boot / "fixup4.dat").write_text("fixup")
    (boot / "bcm2711-rpi-4-b.dtb").write_text("dtb")
    (boot / "overlays").mkdir()
    (boot / "overlays" / "disable-bt.dtbo").write_text("overlay")

    result = run_script(
        "prepare-tftp.sh",
        str(boot),
        str(root),
        env={"AETHER_TFTP_PREFIX": "aether-test"},
    )

    dest = root / "aether-test"
    assert (dest / "start4.elf").read_text() == "firmware"
    assert (dest / "fixup4.dat").read_text() == "fixup"
    assert (dest / "overlays" / "disable-bt.dtbo").read_text() == "overlay"
    assert "kernel=kernel8.img" in (dest / "config.txt").read_text()
    assert str(dest) in result.stdout
    assert not (dest / "start.elf").exists()
    assert not (dest / "fixup.dat").exists()


def test_prepare_tftp_can_download_minimal_firmware_set(tmp_path: pathlib.Path) -> None:
    mirror = tmp_path / "firmware"
    root = tmp_path / "tftp"
    mirror.mkdir()
    (mirror / "start4.elf").write_text("firmware")
    (mirror / "fixup4.dat").write_text("fixup")
    (mirror / "bcm2711-rpi-4-b.dtb").write_text("dtb")
    (mirror / "overlays").mkdir()
    (mirror / "overlays" / "disable-bt.dtbo").write_text("overlay")

    result = run_script(
        "prepare-tftp.sh",
        "--download",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_FIRMWARE_BASE_URL": mirror.as_uri(),
        },
    )

    dest = root / "aether-test"
    assert (dest / "start4.elf").read_text() == "firmware"
    assert (dest / "fixup4.dat").read_text() == "fixup"
    assert (dest / "bcm2711-rpi-4-b.dtb").read_text() == "dtb"
    assert (dest / "overlays" / "disable-bt.dtbo").read_text() == "overlay"
    assert "downloaded Raspberry Pi firmware" in result.stdout
    assert not (dest / "start.elf").exists()
    assert not (dest / "fixup.dat").exists()


def test_prepare_tftp_prunes_stale_fallback_and_self_update_files(tmp_path: pathlib.Path) -> None:
    mirror = tmp_path / "firmware"
    root = tmp_path / "tftp"
    dest = root / "aether-test"
    mirror.mkdir()
    (mirror / "start4.elf").write_text("firmware")
    (mirror / "fixup4.dat").write_text("fixup")
    (mirror / "bcm2711-rpi-4-b.dtb").write_text("dtb")
    (mirror / "overlays").mkdir()
    (mirror / "overlays" / "disable-bt.dtbo").write_text("overlay")
    dest.mkdir(parents=True)
    for stale in ("start.elf", "fixup.dat", "pieeprom.sig", "pieeprom.upd"):
        (dest / stale).write_text("stale")

    run_script(
        "prepare-tftp.sh",
        "--download",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_FIRMWARE_BASE_URL": mirror.as_uri(),
        },
    )

    for stale in ("start.elf", "fixup.dat", "pieeprom.sig", "pieeprom.upd"):
        assert not (dest / stale).exists()


def test_netflash_copies_kernel_and_config_with_hash_verification(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "tftp"
    dest = root / "aether-test"
    dest.mkdir(parents=True)
    kernel = tmp_path / "kernel8.img"
    config = tmp_path / "config.txt"
    kernel.write_bytes(b"test-kernel")
    config.write_text("kernel=kernel8.img\n")

    result = run_script(
        "netflash.sh",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_NETFLASH_SKIP_BUILD": "1",
            "AETHER_KERNEL_IMG": str(kernel),
            "AETHER_CONFIG_TXT": str(config),
        },
    )

    assert (dest / "kernel8.img").read_bytes() == b"test-kernel"
    assert (dest / "config.txt").read_text() == "kernel=kernel8.img\n"
    assert "verified kernel8.img sha256" in result.stdout
    assert "verified config.txt sha256" in result.stdout


def test_serve_netboot_dry_run_is_tftp_only_and_bound_to_interface(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "tftp"
    (root / "aether-test").mkdir(parents=True)

    result = run_script(
        "serve-netboot.sh",
        "en-test0",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_NETBOOT_DRY_RUN": "1",
            "AETHER_TFTP_PROVIDER": "dnsmasq",
            "DNSMASQ": "/usr/local/sbin/dnsmasq",
        },
    )

    assert "--interface=en-test0" in result.stdout
    assert "--enable-tftp" in result.stdout
    assert f"--tftp-root={root}" in result.stdout
    assert "--tftp-no-blocksize" not in result.stdout
    assert "--port=0" in result.stdout
    assert "--dhcp-range" not in result.stdout


def test_serve_netboot_can_disable_blocksize_as_diagnostic(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "tftp"
    (root / "aether-test").mkdir(parents=True)

    result = run_script(
        "serve-netboot.sh",
        "en-test0",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_NETBOOT_DRY_RUN": "1",
            "AETHER_TFTP_PROVIDER": "dnsmasq",
            "AETHER_TFTP_NO_BLOCKSIZE": "1",
            "DNSMASQ": "/usr/local/sbin/dnsmasq",
        },
    )

    assert "--tftp-no-blocksize" in result.stdout


def test_serve_netboot_can_replace_existing_bench_tftp_server(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "tftp"
    (root / "aether-test").mkdir(parents=True)

    result = run_script(
        "serve-netboot.sh",
        "en-test0",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_NETBOOT_DRY_RUN": "1",
            "AETHER_NETBOOT_REPLACE": "1",
            "AETHER_TFTP_PROVIDER": "dnsmasq",
            "DNSMASQ": "/usr/local/sbin/dnsmasq",
        },
    )

    assert "replace existing AetherKernel TFTP providers: yes" in result.stdout
    assert "pkill -f" in read_repo("serve-netboot.sh")
    assert "dnsmasq.*--tftp-root=${TFTP_ROOT}" in read_repo("serve-netboot.sh")
    assert "tftp-now.*serve.*${TFTP_ROOT}" in read_repo("serve-netboot.sh")


def test_serve_netboot_exposes_single_port_and_mtu_diagnostics(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "tftp"
    (root / "aether-test").mkdir(parents=True)

    result = run_script(
        "serve-netboot.sh",
        "en-test0",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_NETBOOT_DRY_RUN": "1",
            "AETHER_TFTP_PROVIDER": "dnsmasq",
            "AETHER_TFTP_SINGLE_PORT": "1",
            "AETHER_TFTP_MTU": "512",
            "DNSMASQ": "/usr/local/sbin/dnsmasq",
        },
    )

    assert "--tftp-single-port" in result.stdout
    assert "--tftp-mtu=512" in result.stdout


def test_serve_netboot_can_use_repo_owned_tftp_provider(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "tftp"
    (root / "aether-test").mkdir(parents=True)

    result = run_script(
        "serve-netboot.sh",
        "en-test0",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_NETBOOT_DRY_RUN": "1",
            "AETHER_TFTP_PROVIDER": "aether",
        },
    )

    assert "TFTP provider: aether" in result.stdout
    assert "aether_tftp.py" in result.stdout
    assert f"--root={root}" in result.stdout
    assert "--block-size=1468" in result.stdout
    assert "--log-file=/tmp/aether-dnsmasq.log" in result.stdout
    assert "--single-port" in result.stdout


def test_serve_netboot_defaults_to_repo_owned_tftp_provider(tmp_path: pathlib.Path) -> None:
    root = tmp_path / "tftp"
    (root / "aether-test").mkdir(parents=True)

    result = run_script(
        "serve-netboot.sh",
        "en-test0",
        str(root),
        env={
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_NETBOOT_DRY_RUN": "1",
        },
    )

    assert "TFTP provider: aether" in result.stdout
    assert "aether_tftp.py" in result.stdout
    assert f"--root={root}" in result.stdout
    assert "--block-size=1468" in result.stdout
    assert "--log-file=/tmp/aether-dnsmasq.log" in result.stdout
    assert "--single-port" in result.stdout


def test_serve_netboot_has_scoped_stop_mode_for_repo_owned_provider() -> None:
    script = read_repo("serve-netboot.sh")

    assert "AETHER_NETBOOT_STOP" in script
    assert 'AETHER_NETBOOT_STOP="$STOP_ONLY"' in script
    assert "stopping existing AetherKernel TFTP providers for $TFTP_ROOT" in script
    assert 'stop_matching_provider "aether_tftp.py.*${TFTP_ROOT}"' in script


def test_serial_reset_dry_run_targets_default_usb_ttl_port() -> None:
    result = run_script("serial-reset.sh", env={"AETHER_SERIAL_RESET_DRY_RUN": "1"})

    assert "/dev/cu.usbserial-B0044J1V" in result.stdout
    assert "payload: r" in result.stdout
    assert "frame: r\\n" in result.stdout


def test_net_iterate_dry_run_describes_stage_reset_watch_loop() -> None:
    result = run_script(
        "net-iterate.sh",
        env={
            "AETHER_NETITERATE_DRY_RUN": "1",
            "AETHER_TFTP_ROOT": "/tmp/aether-root",
            "AETHER_TFTP_PREFIX": "aether-test",
            "AETHER_SERIAL_PORT": "/dev/cu.test",
        },
    )

    assert "./netflash.sh /tmp/aether-root" in result.stdout
    assert "./serial-reset.sh /dev/cu.test" in result.stdout
    assert "/tmp/aether-serial.log" in result.stdout
    assert "/tmp/aether-dnsmasq.log" in result.stdout
    assert "watch TFTP log: /tmp/aether-dnsmasq.log" in result.stdout
    assert "aether-test/" in result.stdout
    assert "attempts: 3" in result.stdout
    assert "timeout per attempt: 150s" in result.stdout


def test_net_iterate_detects_stale_sd_fallback_image() -> None:
    net_iterate = read_repo("net-iterate.sh")

    assert "async heartbeat: timer-backed sleep 1s" in net_iterate
    assert "stale SD fallback image detected" in net_iterate


def test_net_iterate_reports_stale_pre_v11_sd_fallback_without_claiming_netboot() -> None:
    net_iterate = read_repo("net-iterate.sh")

    assert "stale pre-V41 SD fallback image detected" in net_iterate
    assert "stale pre-V42 SD fallback image detected" in net_iterate
    assert "stale pre-V43 SD fallback image detected" in net_iterate
    assert "TFTP kernel fetch was not verified" in net_iterate
    assert 'grep -q "shell ready commands="' in net_iterate
    assert "sd_fallback_seen=1" in net_iterate
    assert "retrying after stale pre-V41 SD fallback" in net_iterate
    assert "retrying after stale pre-V42 SD fallback" in net_iterate
    assert "retrying after stale pre-V43 SD fallback" in net_iterate
    assert "final_exit=3" in net_iterate
    assert 'exit "$final_exit"' in net_iterate


def test_net_iterate_classifies_start4_tftp_failures_as_bootloader_transfer() -> None:
    net_iterate = read_repo("net-iterate.sh")

    assert "failed sending .*/start4\\\\.elf" in net_iterate
    assert "timeout sending .*/start4\\\\.elf" in net_iterate
    assert "kernel was not reached" in net_iterate
    assert "AETHER_TFTP_NO_BLOCKSIZE=1" in net_iterate


def test_net_iterate_accepts_dnsmasq_or_tftp_now_server() -> None:
    net_iterate = read_repo("net-iterate.sh")

    assert "tftp_server_running()" in net_iterate
    assert 'pgrep -f "dnsmasq.*${escaped_root}"' in net_iterate
    assert 'pgrep -f "tftp-now.*serve.*${escaped_root}"' in net_iterate
    assert 'pgrep -f "tftpd.*${escaped_root}"' in net_iterate
    assert 'pgrep -f "aether_tftp.py.*${escaped_root}"' in net_iterate
    assert "TFTP server does not appear to be serving" in net_iterate


def test_netboot_doctor_dry_run_shows_human_reset_gate() -> None:
    result = run_script(
        "netboot-doctor.sh",
        env={
            "AETHER_NETBOOT_DOCTOR_DRY_RUN": "1",
            "AETHER_NETBOOT_INTERFACE": "en-test0",
            "AETHER_NETBOOT_SERVER_IP": "10.99.0.1",
            "AETHER_TFTP_ROOT": "/tmp/aether-root",
            "AETHER_TFTP_PREFIX": "aether-test",
        },
    )

    assert "check interface: en-test0 at 10.99.0.1" in result.stdout
    assert "stage latest image: ./netflash.sh /tmp/aether-root" in result.stdout
    assert "ACTION: reset or power-cycle the Pi once" in result.stdout
    assert "watch TFTP prefix: aether-test/" in result.stdout
    assert "/tmp/aether-serial.log" in result.stdout
    assert "check TFTP root: /tmp/aether-root" in result.stdout
    assert "watch TFTP log:" in result.stdout


def test_netboot_doctor_verifies_runtime_v11_markers() -> None:
    doctor = read_repo("netboot-doctor.sh")

    assert "runtime v4: irq-backed uart shell" in doctor
    assert "runtime v5: diagnostics shell" in doctor
    assert "runtime v6: retained panic/fault records" in doctor
    assert "runtime v7: memory map + frame allocator" in doctor
    assert "runtime v8: allocator guardrails" in doctor
    assert "runtime v9: bounded memory pressure self-tests" in doctor
    assert "runtime v10: explicit guard probes" in doctor
    assert "runtime v11: boot and soak invariants" in doctor
    assert "runtime v21: mmu ownership boundary" in doctor
    assert "shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot" in doctor
    assert "async tick 0x0000000000000000" not in doctor


def test_netboot_doctor_accepts_repo_owned_tftp_provider() -> None:
    doctor = read_repo("netboot-doctor.sh")

    assert "tftp_server_running()" in doctor
    assert 'pgrep -f "aether_tftp.py.*${escaped_root}"' in doctor
    assert "TFTP provider does not appear to be serving" in doctor


def test_netboot_doctor_classifies_start4_tftp_failures_as_bootloader_transfer() -> None:
    doctor = read_repo("netboot-doctor.sh")

    assert "failed sending .*/start4\\\\.elf" in doctor
    assert "timeout sending .*/start4\\\\.elf" in doctor
    assert "kernel was not reached" in doctor
    assert "AETHER_TFTP_NO_BLOCKSIZE=1" in doctor
