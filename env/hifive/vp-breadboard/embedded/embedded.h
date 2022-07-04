#pragma once

#include <QtWidgets>

#include <gpio/gpio-client.hpp>

class Embedded : public QWidget {
	Q_OBJECT

	GpioClient gpio;

	const char* host;
	const char* port;
	bool connected = false;

	void paintEvent(QPaintEvent*) override;


public:
	Embedded(const char* host, const char* port, QWidget *parent);
	~Embedded();

	bool timerUpdate();
	gpio::State getState();
	bool gpioConnected();

public slots:
	void registerIOF_PIN(gpio::PinNumber gpio_offs, GpioClient::OnChange_PIN fun);
	void registerIOF_SPI(gpio::PinNumber gpio_offs, GpioClient::OnChange_SPI fun, bool noresponse);
	void destroyConnection();
	void setBit(gpio::PinNumber gpio_offs, gpio::Tristate state);
};
