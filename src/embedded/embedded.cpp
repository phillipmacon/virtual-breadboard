#include "embedded.h"

#include <QJsonArray>
#include <QPainter>
#include <QMimeData>
#include <QDrag>

using namespace gpio;
using namespace std;

Embedded::Embedded(const std::string& host, const std::string& port) : QWidget(), m_host(host), m_port(port) {
	setMinimumSize(m_windowsize);
	setBackground(m_bkgnd_path);

	m_pin_dialog = new PinOptions(this);
	connect(m_pin_dialog, &PinOptions::pinsChanged, this, &Embedded::pinsChanged);
}

Embedded::~Embedded() = default;

PinNumber Embedded::translatePinToGpioOffs(PinNumber pin) {
	auto pin_obj = m_pins.find(pin);
	if(pin_obj == m_pins.end()) {
		return invalidPin();
	}
	return pin_obj->second.gpio_offs;
}

Embedded::PinRegister Embedded::translateGpioToGlobal(State state) {
	PinRegister ext = 0;
	for(const auto& [global, pin_obj] : m_pins) {
		ext |= (state.pins[pin_obj.gpio_offs] == gpio::Pinstate::HIGH ? 1 : 0) << global;
	}
	return ext;
}

gpio::PinNumber Embedded::invalidPin() {
	static_assert(gpio::max_num_pins < numeric_limits<gpio::PinNumber>::max(),
				  "Invalid Pin hides a valid pin number");
	return numeric_limits<gpio::PinNumber>::max();
}

GPIOPinLayout Embedded::getPins() {
	return m_pins;
}

bool Embedded::isPin(gpio::PinNumber pin) {
	return m_pins.find(pin) != m_pins.end();
}

/* GPIO */

bool Embedded::timerUpdate() { // return: new connection?
	if(m_connected && !m_gpio.update()) {
		emit(connectionLost());
		m_connected = false;
	}
	if(m_connected) {
		for (auto &[global, info]: m_pins) {
			for (auto &iof: info.iofs) {
				if (iof.type == IOFType::SPI) {
					iof.active = m_gpio.state.pins[info.gpio_offs] == gpio::Pinstate::IOF_SPI;
				}
			}
		}
	}
	else {
		m_connected = m_gpio.setupConnection(m_host.c_str(), m_port.c_str());
		if(m_connected) {
			return true;
		}
	}
	return false;
}

Embedded::PinRegister Embedded::getState() {
	return translateGpioToGlobal(m_gpio.state);
}

bool Embedded::gpioConnected() const {
	return m_connected;
}

void Embedded::registerIOF_PIN(PinNumber global, GpioClient::OnChange_PIN fun) {
	gpio::PinNumber gpio_offs = translatePinToGpioOffs(global);
	if(!m_gpio.isIOFactive(gpio_offs)) {
		m_gpio.registerPINOnChange(gpio_offs, fun);
	}
}

void Embedded::registerIOF_SPI(PinNumber global, GpioClient::OnChange_SPI fun, bool no_response) {
	gpio::PinNumber gpio_offs = translatePinToGpioOffs(global);
	if(!m_gpio.isIOFactive(gpio_offs)) {
		m_gpio.registerSPIOnChange(gpio_offs, fun, no_response);
	}
}

void Embedded::closeIOF(PinNumber global) {
	m_gpio.closeIOFunction(translatePinToGpioOffs(global));
}

void Embedded::destroyConnection() {
	m_gpio.destroyConnection();
}

void Embedded::setBit(gpio::PinNumber global, gpio::Tristate state) {
	if(m_connected) {
		m_gpio.setBit(translatePinToGpioOffs(global), state);
	}
}

/* QT */

unsigned Embedded::iconSizeMinimum() {
	return 20;
}

QPoint Embedded::getDistortedPosition(QPoint pos) {
	return {(pos.x()*width())/minimumWidth(), (pos.y()*height())/minimumHeight()};
}

QSize Embedded::getDistortedSize(QSize minimum) {
	return {(minimum.width()*width())/minimumWidth(), (minimum.height()*height())/minimumHeight()};
}

QPoint Embedded::getDistortedPositionPin(gpio::PinNumber global) {
	auto pin = m_pins.find(global);
	if(pin == m_pins.end()) {
		cerr << "Pin " << (int) global << " could not be found on the board" << endl;
		return {0,0};
	}
	return getDistortedPosition(pin->second.pos);
}

void Embedded::paintEvent(QPaintEvent *) {
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	QColor dark("#101010");
	dark.setAlphaF(0.5);
	painter.setBrush(QBrush(dark));

	painter.setFont(QFont("Arial", 3*iconSizeMinimum()/4, QFont::Bold));
	QPen pen(Qt::black);
	painter.setPen(pen);

	for(const auto& [pin, info] : m_pins) {
		painter.drawRect(QRect(getDistortedPosition(info.pos), getDistortedSize(QSize(iconSizeMinimum(), iconSizeMinimum()))));
		painter.drawText(getDistortedPosition(QPoint(info.pos.x(),info.pos.y()+iconSizeMinimum())), QString::number(pin));
	}

	painter.end();
}

void Embedded::resizeEvent(QResizeEvent*) {
	updateBackground();
}

void Embedded::mousePressEvent(QMouseEvent *e) {
	for(const auto& [pin, info] : m_pins) {
		if(QRect(getDistortedPosition(info.pos), getDistortedSize(QSize(iconSizeMinimum(), iconSizeMinimum()))).contains(e->pos())) {
			QSize image_size = getDistortedSize(QSize(iconSizeMinimum(), iconSizeMinimum()));
			QPoint hotspot = QPoint(image_size.width()/2,image_size.height()/2);

			QImage buffer = QImage(image_size, QImage::Format_RGBA8888);
			buffer.fill(Qt::red);

			QByteArray itemData;
			QDataStream dataStream(&itemData, QIODevice::WriteOnly);
			dataStream << (quint8) pin;

			auto *mimeData = new QMimeData;
			mimeData->setData(DRAG_TYPE_CABLE, itemData);
			auto *drag = new QDrag(this);
			drag->setMimeData(mimeData);
			drag->setPixmap(QPixmap::fromImage(buffer));
			drag->setHotSpot(hotspot);

			drag->exec(Qt::MoveAction);
		}
	}
}

/* JSON */

void Embedded::setBackground(QString path) {
	setAutoFillBackground(true);
	m_bkgnd_path = path;
	m_bkgnd = QPixmap(m_bkgnd_path);
	updateBackground();
}

void Embedded::updateBackground() {
	QPixmap new_bkgnd = m_bkgnd.scaled(size(), Qt::IgnoreAspectRatio);
	QPalette palette;
	palette.setBrush(QPalette::Window, new_bkgnd);
	setPalette(palette);
}

void Embedded::fromJSON(QJsonObject json) {
	if(!json.contains("pins") || !json["pins"].isArray()) {
		cerr << "[Embedded] JSON missing pins entry" << endl;
		return;
	}
	if(json.contains("window") && json["window"].isObject()) {
		QJsonObject window = json["window"].toObject();
		unsigned windowsize_x = window["windowsize"].toArray().at(0).toInt();
		unsigned windowsize_y = window["windowsize"].toArray().at(1).toInt();

		setMinimumSize(windowsize_x, windowsize_y);
		setBackground(window["background"].toString());
	}

	m_pins.clear();
	QJsonArray pins = json["pins"].toArray();
	for(const auto& pin_obj : pins) {
		QJsonObject pin = pin_obj.toObject();
		if(!pin.contains("global") || !pin["global"].isDouble()
		|| !pin.contains("gpio_offs") || !pin["gpio_offs"].isDouble()
		|| !pin.contains("pos_x") || !pin["pos_x"].isDouble()
		|| !pin.contains("pos_y") || !pin["pos_y"].isDouble()) {
			cerr << "[Embedded] JSON missing global/gpio offs/position entry for a pin" << endl;
			continue;
		}
		gpio::PinNumber global = pin["global"].toInt();
		gpio::PinNumber gpio_offs = pin["gpio_offs"].toInt();
		list<IOF> iofs;
		if(pin.contains("iofs") && pin["iofs"].isArray()) {
			QJsonArray iofs_obj = pin["iofs"].toArray();
			for (const auto &iof_obj: iofs_obj) {
				QJsonObject iof = iof_obj.toObject();
				if(!iof.contains("type") || !iof["type"].isString()) {
					cerr << "[Embedded] JSON missing type for iof of pin " << (int) global << endl;
					continue;
				}
				bool active = iof["active"].toBool(false);
				IOFType type;
				QString type_str = iof["type"].toString();
				if (type_str == "UART") type = IOFType::UART;
				else if (type_str == "SPI") type = IOFType::SPI;
				else if (type_str == "PWM") type = IOFType::PWM;
				else {
					cerr << "[Embedded] JSON has invalid iof type " << type_str.toStdString() << " for pin " << (int) global << endl;
					continue;
				}
				iofs.push_back(IOF{.type=type, .active=active});
			}
		}
		QPoint pos = QPoint(pin["pos_x"].toInt(), pin["pos_y"].toInt());
		m_pins.emplace(global, GPIOPin{.gpio_offs=gpio_offs,.iofs=iofs, .pos=pos});
	}
}

QJsonObject Embedded::toJSON() {
	QJsonObject json;
	QJsonObject window;
	window["background"] = m_bkgnd_path;
	QJsonArray windowsize;
	windowsize.append(minimumSize().width());
	windowsize.append(minimumSize().height());
	window["windowsize"] = windowsize;
	json["window"] = window;
	QJsonArray pins;
	for(const auto& [global, pin] : m_pins) {
		QJsonObject pin_obj;
		pin_obj["global"] = global;
		pin_obj["gpio_offs"] = pin.gpio_offs;
		QJsonArray iofs;
		for(const auto& iof : pin.iofs) {
			QJsonObject iof_obj;
			QString type;
			switch(iof.type) {
				case IOFType::PWM:
					type = "PWM";
					break;
				case IOFType::SPI:
					type = "SPI";
					break;
				case IOFType::UART:
					type = "UART";
					break;
			}
			iof_obj["type"] = type;
			iof_obj["active"] = iof.active;
			iofs.append(iof_obj);
		}
		if(!iofs.empty()) {
			pin_obj["iofs"] = iofs;
		}
		pin_obj["pos_x"] = pin.pos.x();
		pin_obj["pos_y"] = pin.pos.y();
		pins.append(pin_obj);
	}
	json["pins"] = pins;
	return json;
}

/* DIALOG */

void Embedded::pinsChanged(std::list<std::pair<gpio::PinNumber, IOF>> iofs) {
	for(const auto& [global, iof] : iofs) {
		auto pin = m_pins.find(global);
		if(pin == m_pins.end()) continue;
		IOFType type = iof.type;
		auto pin_iof = std::find_if(pin->second.iofs.begin(), pin->second.iofs.end(),[type](IOF i){
			return type == i.type;
		});
		if(pin_iof == pin->second.iofs.end()) continue;
		pin_iof->active = iof.active;
	}
	emit(pinSettingsChanged(iofs));
}

void Embedded::openPinOptions() {
	m_pin_dialog->setPins(getPins());
	m_pin_dialog->exec();
}
