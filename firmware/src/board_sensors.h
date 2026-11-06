// SensorPort on the board: the two photoresistor dividers on D34 and D35,
// powered from D25.
#pragma once

#include "ports.h"

class BoardSensors : public core::SensorPort {
public:
    void begin();
    core::SensorReading read() override;
    core::SensorReading read_supply_off() override;
};
