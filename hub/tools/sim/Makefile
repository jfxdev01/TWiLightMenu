# Host (PC) build of TWiLight Hub that renders screens to PNG files, to check
# the UI without a console. Usage:
#   make
#   HUB_SIM_SCRIPT="wait 10; shot home; key RIGHT; wait 10; shot home2; quit" ./hubsim
# Screenshots are written to out/.

SRC		:= ../../arm9/source
ASSETS		:= ../../assets
CXX		?= g++
CXXFLAGS	:= -std=gnu++17 -O1 -g -Wall -Wno-unused-function -I$(SRC) -Igen -DHUB_SIM

APP_SOURCES	:= $(filter-out $(SRC)/platform_nds.cpp $(SRC)/net.cpp,$(wildcard $(SRC)/*.cpp))
BIN_FILES	:= $(wildcard $(ASSETS)/*.bin)
GEN_SOURCES	:= $(patsubst $(ASSETS)/%.bin,gen/%_bin.c,$(BIN_FILES))

hubsim: $(APP_SOURCES) platform_sim.cpp $(GEN_SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $(APP_SOURCES) platform_sim.cpp $(GEN_SOURCES) -x none

gen/%_bin.c: $(ASSETS)/%.bin bin2c.py
	@mkdir -p gen
	python3 bin2c.py $< gen

clean:
	rm -rf hubsim gen out
