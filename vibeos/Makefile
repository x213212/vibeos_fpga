CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
RISCV_ARCH ?= rv32im
PROFILE ?= $(profile)
ifeq ($(PROFILE),fpga_minimal)
FPGA_MINIMAL := 1
endif
THIRD_PARTY_DIR = ./third_party
MBEDTLS_DIR = $(THIRD_PARTY_DIR)/mbedtls
MBEDTLS_BUILD_DIR = /tmp/mbedtls-rv-build
MBEDTLS_LIBS = $(MBEDTLS_BUILD_DIR)/library/libmbedtls.a \
               $(MBEDTLS_BUILD_DIR)/library/libmbedcrypto.a \
               $(MBEDTLS_BUILD_DIR)/library/libmbedx509.a \
               $(MBEDTLS_BUILD_DIR)/tf-psa-crypto/core/libtfpsacrypto.a

LIBSSH2_DIR = $(THIRD_PARTY_DIR)/libssh2
NSFB_DIR = $(THIRD_PARTY_DIR)/libnsfb
WCAP_DIR = $(THIRD_PARTY_DIR)/libwapcaplet
PUTIL_DIR = $(THIRD_PARTY_DIR)/libparserutils
HUB_DIR = $(THIRD_PARTY_DIR)/libhubbub
TCC_DIR = $(THIRD_PARTY_DIR)/tcc-riscv32
LWIP_DIR = $(THIRD_PARTY_DIR)/lwip
GBEMU_DIR = $(THIRD_PARTY_DIR)/gbemu
WRP_DIR = $(THIRD_PARTY_DIR)/wrp

CFLAGS = -nostdlib -fno-builtin -mcmodel=medany -march=$(RISCV_ARCH) -mabi=ilp32 -DLWIP_NO_CTYPE_H -DWITHOUT_ICONV_FILTER -g -Wall -w \
         -mno-relax \
         -I./ -I./runtime/jit -I./ports/mbedtls -I$(LWIP_DIR)/src/include -I$(LWIP_DIR)/src/core -I$(LWIP_DIR)/src/netif \
         -I./apps/gbemu -I./apps/netsurf -I./apps/net/wget -I./apps/ssh -I./drivers/audio -I./drivers/sd \
         -I$(GBEMU_DIR)/include \
         -I$(LIBSSH2_DIR)/include -I$(LIBSSH2_DIR)/src \
         -I$(NSFB_DIR)/include -I$(NSFB_DIR)/src \
         -I$(WCAP_DIR)/include \
         -I$(PUTIL_DIR)/include -I$(PUTIL_DIR)/src \
         -I$(HUB_DIR)/include -I$(HUB_DIR)/src \
         -I$(TCC_DIR) -DTCC_TARGET_RISCV32 -DONE_SOURCE=1 -DCONFIG_TCC_STATIC -DCONFIG_TCC_PREDEFS=1 \
         -include apps/netsurf/netsurf_port.h \
         -Dprintf=lib_printf \
         -I$(MBEDTLS_DIR)/include -I$(MBEDTLS_DIR)/tf-psa-crypto/include -I$(MBEDTLS_DIR)/tf-psa-crypto/core -I$(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/include -I$(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src -I$(MBEDTLS_DIR)/tf-psa-crypto/utilities \
         -DMBEDTLS_CONFIG_FILE=\"$(MBEDTLS_DIR)/configs/config-suite-b.h\" \
         -DMBEDTLS_USER_CONFIG_FILE=\"ports/mbedtls/mbedtls_os_config.h\" \
         -DTF_PSA_CRYPTO_CONFIG_FILE=\"$(MBEDTLS_DIR)/configs/crypto-config-suite-b.h\" \
         -DTF_PSA_CRYPTO_USER_CONFIG_FILE=\"ports/mbedtls/tf_psa_crypto_os_config.h\" \
         -DMBEDTLS_MD_C -DMBEDTLS_MD_LIGHT -DMBEDTLS_CIPHER_C -DMBEDTLS_RSA_C \
         -DHAVE_CONFIG_H \
         -DHOST_BUILD_YEAR=$(shell date +%Y) -DHOST_BUILD_MONTH=$(shell date +%-m) -DHOST_BUILD_DAY=$(shell date +%-d) \
         -DHOST_BUILD_HOUR=$(shell date +%-H) -DHOST_BUILD_MIN=$(shell date +%-M) -DHOST_BUILD_SEC=$(shell date +%-S)

ifeq ($(FPGA_MINIMAL),1)
CFLAGS += -DFPGA_MINIMAL=1
FPGA_MTIME_HZ ?= 50000000
CFLAGS += -DFPGA_MTIME_HZ=$(FPGA_MTIME_HZ)ULL
endif

OPT_FLAGS = -O2 -funroll-loops -ffast-math -ftree-vectorize -ffunction-sections -fdata-sections
GDB = riscv64-unknown-elf-gdb
PYTHON ?= python3
PERL ?= perl
BUILD_DIR = .build
FIRMWARE_HEX ?= ../project_2/firmware.hex

# NetSurf Core Libraries Sources
NSFB_SOURCES = $(NSFB_DIR)/src/libnsfb.c $(NSFB_DIR)/src/surface/surface.c $(NSFB_DIR)/src/surface/ram.c \
               $(NSFB_DIR)/src/plot/api.c $(NSFB_DIR)/src/plot/8bpp.c $(NSFB_DIR)/src/plot/16bpp.c \
               $(NSFB_DIR)/src/plot/32bpp-xbgr8888.c $(NSFB_DIR)/src/plot/32bpp-xrgb8888.c \
               $(NSFB_DIR)/src/plot/generic.c $(NSFB_DIR)/src/plot/util.c \
               $(NSFB_DIR)/src/palette.c $(NSFB_DIR)/src/cursor.c

PUTIL_SOURCES = $(WCAP_DIR)/src/libwapcaplet.c \
                $(PUTIL_DIR)/src/charset/aliases.c \
                $(PUTIL_DIR)/src/input/inputstream.c $(PUTIL_DIR)/src/input/filter.c \
                 $(PUTIL_DIR)/src/charset/codec.c \
                $(PUTIL_DIR)/src/charset/codecs/codec_utf8.c \
                $(PUTIL_DIR)/src/charset/codecs/codec_utf16.c \
                $(PUTIL_DIR)/src/charset/codecs/codec_8859.c \
                $(PUTIL_DIR)/src/charset/codecs/codec_ascii.c \
                $(PUTIL_DIR)/src/charset/codecs/codec_ext8.c \
                $(PUTIL_DIR)/src/charset/encodings/utf8.c \
                $(PUTIL_DIR)/src/charset/encodings/utf16.c \
                $(PUTIL_DIR)/src/utils/stack.c $(PUTIL_DIR)/src/utils/vector.c \
                $(PUTIL_DIR)/src/utils/buffer.c $(PUTIL_DIR)/src/utils/errors.c

HUB_SOURCES = $(HUB_DIR)/src/parser.c $(HUB_DIR)/src/tokeniser/tokeniser.c $(HUB_DIR)/src/tokeniser/entities.c \
              $(HUB_DIR)/src/utils/string.c \
              $(HUB_DIR)/src/charset/detect.c \
              $(HUB_DIR)/src/treebuilder/treebuilder.c $(HUB_DIR)/src/treebuilder/initial.c \
              $(HUB_DIR)/src/treebuilder/before_html.c $(HUB_DIR)/src/treebuilder/before_head.c \
              $(HUB_DIR)/src/treebuilder/in_head.c $(HUB_DIR)/src/treebuilder/in_head_noscript.c \
              $(HUB_DIR)/src/treebuilder/after_head.c $(HUB_DIR)/src/treebuilder/in_body.c \
              $(HUB_DIR)/src/treebuilder/after_body.c $(HUB_DIR)/src/treebuilder/after_after_body.c \
              $(HUB_DIR)/src/treebuilder/in_table.c $(HUB_DIR)/src/treebuilder/in_caption.c \
              $(HUB_DIR)/src/treebuilder/in_column_group.c $(HUB_DIR)/src/treebuilder/in_table_body.c \
              $(HUB_DIR)/src/treebuilder/in_row.c $(HUB_DIR)/src/treebuilder/in_cell.c \
              $(HUB_DIR)/src/treebuilder/in_select.c $(HUB_DIR)/src/treebuilder/in_select_in_table.c \
              $(HUB_DIR)/src/treebuilder/in_frameset.c $(HUB_DIR)/src/treebuilder/after_frameset.c \
              $(HUB_DIR)/src/treebuilder/after_after_frameset.c $(HUB_DIR)/src/treebuilder/in_foreign_content.c \
              $(HUB_DIR)/src/treebuilder/generic_rcdata.c $(HUB_DIR)/src/treebuilder/element-type.c

CORE_SOURCES = start.s sys.s mem.s lib.c timer.c task.c os.c fs.c runtime/jit/jit_debugger.c app_runtime.c trap.c lock.c plic.c string.c vga.c font.c alloc.c \
      runtime/jit/tcc_glue.c $(TCC_DIR)/libtcc.c ports/mbedtls/mbedtls_entropy_compat.c ports/mbedtls/mbedtls_port.c ports/mbedtls/mbedtls_compat.c compat_time.c

USER_SOURCES = user.c user_terminal.c user_gui.c user_fs_shell.c user_editor.c user_graphics.c user_cmd.c user_utils.c

APP_SOURCES = apps/netsurf/netsurf.c apps/editor/tool_editor.c apps/editor/tool_asm.c apps/net/wget/user_wget.c apps/net/ntp/user_ntp.c apps/ssh/ssh_client.c \
      apps/demo3d/demo3d.c apps/gbemu/gbemu_runtime.c apps/gbemu/gbemu_audio_stub.c

DRIVER_SOURCES = drivers/virtio/virtio_disk.c drivers/virtio/virtio_net.c drivers/virtio/virtio_input.c drivers/ps_gem.c drivers/audio/ac97_audio.c \
      drivers/sd/sdio_api.c drivers/sd/sd_fat.c

GBEMU_CORE_SOURCES = $(GBEMU_DIR)/lib/cart.c $(GBEMU_DIR)/lib/cpu.c $(GBEMU_DIR)/lib/cpu_fetch.c $(GBEMU_DIR)/lib/cpu_proc.c \
      $(GBEMU_DIR)/lib/cpu_util.c $(GBEMU_DIR)/lib/dbg.c $(GBEMU_DIR)/lib/dma.c $(GBEMU_DIR)/lib/gamepad.c \
      $(GBEMU_DIR)/lib/interrupts.c $(GBEMU_DIR)/lib/instructions.c $(GBEMU_DIR)/lib/io.c $(GBEMU_DIR)/lib/lcd.c \
      $(GBEMU_DIR)/lib/ppu.c $(GBEMU_DIR)/lib/ppu_pipeline.c $(GBEMU_DIR)/lib/ppu_sm.c $(GBEMU_DIR)/lib/ram.c \
      $(GBEMU_DIR)/lib/stack.c $(GBEMU_DIR)/lib/bus.c $(GBEMU_DIR)/lib/timer.c

LWIP_SOURCES = $(LWIP_DIR)/src/core/def.c $(LWIP_DIR)/src/core/init.c $(LWIP_DIR)/src/core/mem.c $(LWIP_DIR)/src/core/memp.c \
      $(LWIP_DIR)/src/core/altcp.c $(LWIP_DIR)/src/core/altcp_alloc.c $(LWIP_DIR)/src/core/altcp_tcp.c \
      $(LWIP_DIR)/src/core/netif.c $(LWIP_DIR)/src/core/pbuf.c $(LWIP_DIR)/src/core/raw.c $(LWIP_DIR)/src/core/sys.c \
      $(LWIP_DIR)/src/core/tcp.c $(LWIP_DIR)/src/core/tcp_in.c $(LWIP_DIR)/src/core/tcp_out.c $(LWIP_DIR)/src/core/udp.c \
      $(LWIP_DIR)/src/core/ip.c $(LWIP_DIR)/src/core/dns.c $(LWIP_DIR)/src/netif/ethernet.c $(LWIP_DIR)/src/netif/lowpan6.c \
      $(LWIP_DIR)/src/core/timeouts.c $(LWIP_DIR)/src/core/inet_chksum.c \
      $(LWIP_DIR)/src/core/ipv4/ip4.c $(LWIP_DIR)/src/core/ipv4/acd.c $(LWIP_DIR)/src/core/ipv4/autoip.c \
      $(LWIP_DIR)/src/core/ipv4/igmp.c $(LWIP_DIR)/src/core/ipv4/ip4_addr.c $(LWIP_DIR)/src/core/ipv4/icmp.c \
      $(LWIP_DIR)/src/core/ipv4/dhcp.c $(LWIP_DIR)/src/core/ipv4/etharp.c $(LWIP_DIR)/src/core/ipv4/ip4_frag.c \
      $(LWIP_DIR)/src/apps/sntp/sntp.c \
      $(LWIP_DIR)/src/apps/altcp_tls/altcp_tls_mbedtls.c $(LWIP_DIR)/src/apps/altcp_tls/altcp_tls_mbedtls_mem.c

THIRD_PARTY_SOURCES = $(GBEMU_CORE_SOURCES) \
      $(NSFB_SOURCES) $(PUTIL_SOURCES) $(HUB_SOURCES) \
      $(LIBSSH2_DIR)/src/agent.c $(LIBSSH2_DIR)/src/bcrypt_pbkdf.c $(LIBSSH2_DIR)/src/chacha.c \
      $(LIBSSH2_DIR)/src/channel.c $(LIBSSH2_DIR)/src/cipher-chachapoly.c $(LIBSSH2_DIR)/src/comp.c \
      $(LIBSSH2_DIR)/src/crypt.c $(LIBSSH2_DIR)/src/global.c $(LIBSSH2_DIR)/src/hostkey.c \
      $(LIBSSH2_DIR)/src/keepalive.c $(LIBSSH2_DIR)/src/kex.c $(LIBSSH2_DIR)/src/knownhost.c \
      $(LIBSSH2_DIR)/src/mac.c $(LIBSSH2_DIR)/src/mbedtls.c $(LIBSSH2_DIR)/src/misc.c \
      $(LIBSSH2_DIR)/src/packet.c $(LIBSSH2_DIR)/src/pem.c $(LIBSSH2_DIR)/src/poly1305.c \
      $(LIBSSH2_DIR)/src/publickey.c $(LIBSSH2_DIR)/src/scp.c $(LIBSSH2_DIR)/src/session.c \
      $(LIBSSH2_DIR)/src/sftp.c $(LIBSSH2_DIR)/src/transport.c $(LIBSSH2_DIR)/src/userauth.c \
      $(LIBSSH2_DIR)/src/userauth_kbd_packet.c $(LIBSSH2_DIR)/src/version.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/extras/md.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/extras/pk.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/extras/pk_wrap.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/extras/pkparse.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/extras/pk_rsa.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/cipher.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/cipher_wrap.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/rsa_alt_helpers.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/rsa.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/aes.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/block_cipher.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/sha1.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/sha256.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/sha512.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/md5.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/bignum.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/bignum_core.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/psa_util_internal.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/ctr_drbg.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/drivers/builtin/src/hmac_drbg.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/utilities/base64.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/utilities/pem.c \
      $(MBEDTLS_DIR)/tf-psa-crypto/utilities/oid.c \
      $(LWIP_SOURCES)

OBJ = $(CORE_SOURCES) $(USER_SOURCES) $(APP_SOURCES) $(DRIVER_SOURCES) $(THIRD_PARTY_SOURCES)

QEMU = qemu-system-riscv32
HOST_DISPLAY := $(DISPLAY)
SSH_CLIENT_IP := $(word 1,$(SSH_CONNECTION))
QEMU_DISPLAY ?= $(if $(strip $(HOST_DISPLAY)),$(HOST_DISPLAY),$(if $(strip $(SSH_CLIENT_IP)),$(SSH_CLIENT_IP):0.0,:0))
QEMU_DISPLAY_BACKEND ?= gtk
QEMU_GUEST_ERRORS ?= 0
ifeq ($(QEMU_GUEST_ERRORS),1)
QEMU_LOG_FLAGS = -d guest_errors
endif
QEMU_RUN_ENV =
ifneq ($(strip $(QEMU_DISPLAY)),)
QEMU_RUN_ENV += DISPLAY=$(QEMU_DISPLAY)
endif
QFLAGS = -smp 1 -machine virt -m 1G -bios none \
         -drive if=none,format=raw,file=hdd.dsk,id=x0 \
         -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
         -device virtio-net-device,netdev=net0,mac=52:54:00:12:34:56,bus=virtio-mmio-bus.1,csum=off,guest_csum=off,gso=off,guest_tso4=off,guest_tso6=off,guest_ecn=off,guest_ufo=off,host_tso4=off,host_tso6=off,host_ecn=off,host_ufo=off,mrg_rxbuf=off,rx_queue_size=256,tx_queue_size=256 \
         -device virtio-keyboard-device,bus=virtio-mmio-bus.2 \
         -device virtio-tablet-device,bus=virtio-mmio-bus.3 \
         -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
         $(QEMU_LOG_FLAGS) \
         -display $(QEMU_DISPLAY_BACKEND) \
         -device VGA \
         -monitor telnet:localhost:4321,server,nowait

ENABLE_AUDIO ?= 0
PULSE_SERVER_PATH ?= /mnt/wslg/PulseServer
ifeq ($(ENABLE_AUDIO),1)
QEMU_AUDIO_FLAGS = -audiodev pa,id=snd0,server=unix:$(PULSE_SERVER_PATH) -device AC97,audiodev=snd0
endif

ENABLE_HDD_BACKUP ?= 0
HDD_BACKUP_DIR ?= hdd_backups
HDD_BACKUP_KEEP ?= 10

all: os.bin

build: os.bin

.PHONY: fpga_minimal fpga

fpga_minimal:
	$(MAKE) profile=fpga_minimal os.bin

fpga: fpga_minimal

test: os.elf qemu

rebuild: clean os.elf

ALL_SRCS = $(OBJ)
OBJS = $(patsubst %.s,$(BUILD_DIR)/%.o,$(patsubst %.c,$(BUILD_DIR)/%.o,$(ALL_SRCS)))
DEPS = $(OBJS:.o=.d)

GENERATED_FILES = \
    $(LIBSSH2_DIR)/src/libssh2_config.h \
    $(HUB_DIR)/src/tokeniser/entities.inc \
    $(HUB_DIR)/src/treebuilder/autogenerated-element-type.c

$(OBJS): | $(GENERATED_FILES)

$(HUB_DIR)/src/tokeniser/entities.inc: $(HUB_DIR)/build/make-entities.pl $(HUB_DIR)/build/Entities
	cd $(HUB_DIR) && $(PERL) build/make-entities.pl

$(HUB_DIR)/src/treebuilder/autogenerated-element-type.c: $(HUB_DIR)/src/treebuilder/element-type.gperf tools/gen_hubbub_element_type.py
	$(PYTHON) tools/gen_hubbub_element_type.py $< $@

$(LIBSSH2_DIR)/src/libssh2_config.h: tools/gen_libssh2_config.py
	$(PYTHON) tools/gen_libssh2_config.py $@

os.elf: $(OBJS) $(MBEDTLS_LIBS)
	$(CC) $(CFLAGS) $(OPT_FLAGS) -Wl,--gc-sections -T os.ld -o os.elf $(OBJS) $(MBEDTLS_LIBS) -lgcc

os.bin: os.elf
	$(OBJCOPY) -O binary os.elf os.bin

firmware.hex: os.bin
	$(PYTHON) tools/bin_to_readmemh.py os.bin $(FIRMWARE_HEX)
	@echo "Wrote $(FIRMWARE_HEX)"

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(OPT_FLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -include apps/netsurf/netsurf_port.h,$(CFLAGS)) $(OPT_FLAGS) -x assembler-with-cpp -MMD -MP -c $< -o $@

-include $(DEPS)

QEMU_DEPS = os.elf hdd.dsk
ifeq ($(ENABLE_HDD_BACKUP),1)
QEMU_DEPS += backup-hdd
endif

qemu: $(QEMU_DEPS)
	@if [ "$(ENABLE_AUDIO)" = "1" ]; then \
		if [ -S "$(PULSE_SERVER_PATH)" ]; then \
			echo "QEMU audio: PulseAudio socket found at $(PULSE_SERVER_PATH)"; \
			$(QEMU_RUN_ENV) PULSE_SERVER=unix:$(PULSE_SERVER_PATH) $(QEMU) $(QFLAGS) $(QEMU_AUDIO_FLAGS) -kernel os.elf; \
		else \
			echo "QEMU audio: PulseAudio socket not found at $(PULSE_SERVER_PATH), falling back to no audio."; \
			$(QEMU_RUN_ENV) $(QEMU) $(QFLAGS) -kernel os.elf; \
		fi; \
	else \
		$(QEMU_RUN_ENV) $(QEMU) $(QFLAGS) -kernel os.elf; \
	fi

qemu-backup: ENABLE_HDD_BACKUP = 1
qemu-backup: qemu

qemu-sdl: QEMU_DISPLAY_BACKEND = sdl
qemu-sdl: qemu

backup-hdd: hdd.dsk
	@mkdir -p $(HDD_BACKUP_DIR)
	@backup="$(HDD_BACKUP_DIR)/hdd-$$(date +%Y%m%d-%H%M%S).dsk"; \
	cp -p hdd.dsk "$$backup"; \
	echo "HDD backup: $$backup"; \
	ls -1t $(HDD_BACKUP_DIR)/hdd-*.dsk 2>/dev/null | tail -n +$$(($(HDD_BACKUP_KEEP) + 1)) | xargs -r rm -f

clean:
	rm -rf $(BUILD_DIR) *.elf *.img *.bin $(GENERATED_FILES)

hdd.dsk:
	dd if=/dev/urandom of=hdd.dsk bs=1M count=32
