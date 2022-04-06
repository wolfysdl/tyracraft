EE_BIN = tyracraft.elf

TYRA_DIR = $(TYRA)

EE_OBJS =											\
	3libs/SimplexNoise.o							\
	managers/terrain_manager.o						\
	managers/block_manager.o						\
	objects/World.o									\
	objects/Block.o									\
	objects/chunck.o								\
	objects/player.o								\
	utils.o											\
	ui.o											\
	camera.o										\
	start.o											\
	main.o

EE_LIBS = -ltyra

all: $(EE_BIN)
	$(EE_STRIP) --strip-all $(EE_BIN)
	mkdir bin/
	mv $(EE_BIN) bin/$(EE_BIN)
	cp -a assets/* bin/
	rm $(EE_OBJS)

rebuild-engine:
	cd $(TYRA_DIR) && make clean && make

clean:
	rm -fr bin/ $(EE_OBJS)

run: $(EE_BIN)
	killall -v ps2client || true
	ps2client reset
	ps2client reset
	$(EE_STRIP) --strip-all $(EE_BIN)
	rm $(EE_OBJS)
	ps2client execee host:$(EE_BIN)

include ./Makefile.pref
