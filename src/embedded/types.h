#pragma once

#include <gpio-client.hpp>

#include <list>
#include <QPoint>

enum class IOFType {
	SPI,
	UART,
	PWM
};

struct IOF {
	IOFType type = IOFType::SPI;
	bool active=false;
};

struct GPIOPin {
	gpio::PinNumber gpio_offs;
	std::list<IOF> iofs;
	QPoint pos;
};
typedef std::unordered_map<gpio::PinNumber, GPIOPin> GPIOPinLayout; // GLOBAL to information