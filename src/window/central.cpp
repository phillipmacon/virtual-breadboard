#include "central.h"

#include <QVBoxLayout>
#include <QTimer>

/* Constructor */

Central::Central(const std::string host, const std::string port, QWidget *parent) : QWidget(parent) {
	breadboard = new Breadboard();
	embedded = new Embedded(host, port);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(embedded);
	layout->addWidget(breadboard);

	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &Central::timerUpdate);
	timer->start(250);

	connect(breadboard, &Breadboard::registerIOF_PIN, embedded, &Embedded::registerIOF_PIN);
	connect(breadboard, &Breadboard::registerIOF_SPI, embedded, &Embedded::registerIOF_SPI);
	connect(breadboard, &Breadboard::closeAllIOFs, this, &Central::closeAllIOFs);
	connect(breadboard, &Breadboard::closeDeviceIOFs, this, &Central::closeDeviceIOFs);
	connect(breadboard, &Breadboard::setBit, embedded, &Embedded::setBit);
	connect(embedded, &Embedded::connectionLost, [this](){
		emit(connectionUpdate(false));
	});
	connect(this, &Central::connectionUpdate, breadboard, &Breadboard::connectionUpdate);
}

Central::~Central() {
}

void Central::destroyConnection() {
	embedded->destroyConnection();
}

bool Central::toggleDebug() {
	return breadboard->toggleDebug();
}

void Central::closeAllIOFs(std::vector<gpio::PinNumber> gpio_offs) {
	for(gpio::PinNumber gpio : gpio_offs) {
		embedded->closeIOF(gpio);
	}
	breadboard->clearConnections();
}

void Central::closeDeviceIOFs(std::vector<gpio::PinNumber> gpio_offs, DeviceID device) {
	for(gpio::PinNumber gpio : gpio_offs) {
		embedded->closeIOF(gpio);
	}
	breadboard->removeDeviceObjects(device);
}

/* LOAD */

void Central::loadJSON(QString file) {
	emit(sendStatus("Loading config file " + file, 10000));
    if(!breadboard->loadConfigFile(file)) {
        std::cerr << "[Central] Could not open config file " << std::endl;
        return;
    }
	if(breadboard->isBreadboard()) {
		embedded->show();
	}
	else {
		embedded->hide();
	}
	if(embedded->gpioConnected()) {
		breadboard->connectionUpdate(true);
	}
}

void Central::saveJSON(QString file) {
	breadboard->saveConfigFile(file);
}

void Central::clearBreadboard() {
	emit(sendStatus("Clearing breadboard", 10000));
	breadboard->clear();
	embedded->show();
}

void Central::loadLUA(std::string dir, bool overwrite_integrated_devices) {
	breadboard->additionalLuaDir(dir, overwrite_integrated_devices);
}

/* Timer */

void Central::timerUpdate() {
 	bool reconnect = embedded->timerUpdate();
	if(reconnect) {
		emit(connectionUpdate(true));
	}
	if(embedded->gpioConnected()) {
		breadboard->timerUpdate(embedded->getState());
	}
}
