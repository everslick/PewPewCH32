#ifndef STATE_MACHINE_H
#define STATE_MACHINE_H

#include <stdint.h>
#include "LedController.h"
#include "PicoSWIO.h"
#include "RVDebug.h"
#include "WCHFlash.h"

class DisplayController;

#ifdef FIRMWARE_INVENTORY_ENABLED
  #include "firmware_inventory.h"
#endif

// System States
enum SystemState {
    STATE_IDLE,
    STATE_CHECKING_TARGET,
    STATE_PROGRAMMING,
    STATE_CYCLING_FIRMWARE,
    STATE_SUCCESS,
    STATE_ERROR
};

class StateMachine {
public:
    StateMachine(LedController* led, RVDebug* rvd, WCHFlash* flash);
    ~StateMachine();
    
    // State management
    SystemState getCurrentState() const { return current_state; }
    void setState(SystemState state);
    
    // State processing
    void process();
    
    // Actions
    void startProgramming();
    void startTargetCheck();
    void cycleFirmware();
    
    // Display integration
    void setDisplayController(DisplayController* dc) { display_controller = dc; }
    void setDebugBus(PicoSWIO* swio, int pin) { debug_swio = swio; swio_pin = pin; }

    // Configuration
    void setCurrentFirmwareIndex(int index) { current_firmware_index = index; }
    int getCurrentFirmwareIndex() const { return current_firmware_index; }
    const char* getCurrentMenuName() const;
    static const char* getStateName(SystemState state);
    
private:
    SystemState current_state;
    uint32_t state_timer;
    int current_firmware_index;
    
    LedController* led_controller;
    DisplayController* display_controller;
    RVDebug* rv_debug;
    WCHFlash* wch_flash;
    PicoSWIO* debug_swio;
    int swio_pin;
    
    // Helper functions
    bool haltWithTimeout(uint32_t timeout_ms);
#ifdef FIRMWARE_INVENTORY_ENABLED
    bool programFirmware(const firmware_info_t* fw);
#endif
    bool programFlash(const uint8_t* data, size_t size, uint32_t base_address);
    bool wipeChip();
    bool rebootChip();
};

#endif // STATE_MACHINE_H