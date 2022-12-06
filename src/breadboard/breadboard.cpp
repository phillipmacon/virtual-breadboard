#include "breadboard.h"

#include <QTimer>
#include <QInputDialog>

using namespace std;

Breadboard::Breadboard() : QWidget() {
	setFocusPolicy(Qt::StrongFocus);
	setAcceptDrops(true);
	setMouseTracking(true);

	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]{update();});
	timer->start(1000/30);

	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this, &Breadboard::openContextMenu);
    m_devices_menu = new QMenu(this);
	auto *delete_device = new QAction("Delete");
	connect(delete_device, &QAction::triggered, this, &Breadboard::removeActiveDevice);
	m_devices_menu->addAction(delete_device);
    auto *scale_device = new QAction("Scale");
    connect(scale_device, &QAction::triggered, this, &Breadboard::scaleActiveDevice);
    m_devices_menu->addAction(scale_device);
    auto *open_configurations = new QAction("Device Configurations");
    connect(open_configurations, &QAction::triggered, this, &Breadboard::openDeviceConfigurations);
    m_devices_menu->addAction(open_configurations);

    m_device_configurations = new DeviceConfigurations(this);
    connect(m_device_configurations, &DeviceConfigurations::keysChanged, this, &Breadboard::updateKeybinding);
    connect(m_device_configurations, &DeviceConfigurations::configChanged, this, &Breadboard::updateConfig);
    connect(m_device_configurations, &DeviceConfigurations::pinsChanged, this, &Breadboard::updatePins);
    m_error_dialog = new QErrorMessage(this);

    m_add_device = new QMenu(this);
	for(const DeviceClass& device : m_factory.getAvailableDevices()) {
		auto *device_action = new QAction(QString::fromStdString(device));
		connect(device_action, &QAction::triggered, [this, device](){
			addDevice(device, mapFromGlobal(m_add_device->pos()));
		});
		m_add_device->addAction(device_action);
	}

	setMinimumSize(DEFAULT_SIZE);
	setBackground(DEFAULT_PATH);
}

Breadboard::~Breadboard() = default;

bool Breadboard::isBreadboard() { return m_bkgnd_path == DEFAULT_PATH; }
bool Breadboard::toggleDebug() {
    m_debugmode = !m_debugmode;
	return m_debugmode;
}

/* DEVICE */

QPoint Breadboard::checkDevicePosition(const DeviceID& id, const QImage& buffer, int scale, QPoint position, QPoint hotspot) {
	QPoint upper_left = position - hotspot;
	QRect device_bounds = QRect(upper_left, getDistortedGraphicBounds(buffer, scale).size());

	if(isBreadboard()) {
		if(getRasterBounds().intersects(device_bounds)) {
			QPoint dropPositionRaster = getAbsolutePosition(getRow(position), getIndex(position));
			upper_left = dropPositionRaster - getDeviceAbsolutePosition(getDeviceRow(hotspot),
					getDeviceIndex(hotspot));
			device_bounds = QRect(upper_left, device_bounds.size());
		} else {
			cerr << "[Breadboard] Device position invalid: Device should be at least partially on raster." << endl;
			return {-1,-1};
		}
	}

	if(!rect().contains(device_bounds)) {
		cerr << "[Breadboard] Device position invalid: Device may not leave window view." << endl;
		return {-1,-1};
	}

	for(const auto& [id_it, device_it] : m_devices) {
		if(id_it == id) continue;
		if(getDistortedGraphicBounds(device_it->getBuffer(),
				device_it->getScale()).intersects(device_bounds)) {
			cerr << "[Breadboard] Device position invalid: Overlaps with other device." << endl;
			return {-1,-1};
		}
	}
	return getMinimumPosition(upper_left);
}

bool Breadboard::moveDevice(Device *device, QPoint position, QPoint hotspot) {
	if(!device) return false;
	unsigned scale = device->getScale();
	if(!scale) scale = 1;

	QPoint upper_left = checkDevicePosition(device->getID(), device->getBuffer(), scale, position, hotspot);

	if(upper_left.x()<0) {
		cerr << "[Breadboard] New device position is invalid." << endl;
		return false;
	}

	device->getBuffer().setOffset(upper_left);
	device->setScale(scale);

    if(!device->m_pin) return true;

    Device::PIN_Interface::PinLayout device_layout = device->m_pin->getPinLayout();
    for(auto const& [device_pin, desc] : device_layout) {
        DeviceConnection new_connection = DeviceConnection{
                .id = device->getID(),
                .pin = device_pin
        };
        Row bb_row;
        if(isBreadboard()) {
            QPoint pos_on_device = getDeviceAbsolutePosition(desc.row, desc.index);
            QPoint pos_on_raster = getDistortedPosition(upper_left) + pos_on_device;
            bb_row = getRow(pos_on_raster);
        }
        else if (m_raster.size() < std::numeric_limits<unsigned>::max()) {
                bb_row = m_devices.size();
        }
        else {
            std::set<unsigned> used_rows;
            for(auto const& [row, content] : m_raster) {
                used_rows.insert(row);
            }
            for(unsigned used_row : used_rows) {
                if(used_row > bb_row) {
                    break;
                }
                bb_row++;
            }
        }
        auto row_obj = m_raster.find(bb_row);
        if(row_obj != m_raster.end()) {
            row_obj->second.devices.push_back(new_connection);
        } else {
            RowContent new_content;
            new_content.devices.push_back(new_connection);
            m_raster.emplace(bb_row, new_content);
        }
        createRowConnections(bb_row);
    }
	return true;
}

bool Breadboard::addDevice(const DeviceClass& classname, QPoint pos, DeviceID id) {
	if(id.empty()) {
		if(m_devices.size() < std::numeric_limits<unsigned>::max()) {
			id = std::to_string(m_devices.size());
		}
		else {
			std::set<unsigned> used_ids;
			for(auto const& [id_it, device_it] : m_devices) {
				used_ids.insert(std::stoi(id_it));
			}
			unsigned id_int = 0;
			for(unsigned used_id : used_ids) {
				if(used_id > id_int)
					break;
				id_int++;
			}
			id = std::to_string(id_int);
		}
	}

	if(id.empty()) {
		cerr << "[Breadboard] Device ID cannot be empty string!" << endl;
		return false;
	}
	if(!m_factory.deviceExists(classname)) {
		cerr << "[Breadboard] Class name '" << classname << "' invalid." << endl;
		return false;
	}
	if(m_devices.find(id) != m_devices.end()) {
		cerr << "[Breadboard] Another device with the ID '" << id << "' already exists!" << endl;
		return false;
	}

	unique_ptr<Device> device = m_factory.instantiateDevice(id, classname);
	device->createBuffer(iconSizeMinimum(), pos);
	if(moveDevice(device.get(), pos)) {
		m_devices.insert(make_pair(id, std::move(device)));
		return true;
	}
	cerr << "[Breadboard] Could not place new " << classname << " device" << endl;
	return false;
}

/* Context Menu */

void Breadboard::openContextMenu(QPoint pos) {
	for(auto const& [id, device] : m_devices) {
		if(getDistortedGraphicBounds(device->getBuffer(), device->getScale()).contains(pos)) {
            m_menu_device_id = id;
			m_devices_menu->popup(mapToGlobal(pos));
			return;
		}
	}
	if(isBreadboard() && !isOnRaster(pos)) return;
	m_add_device->popup(mapToGlobal(pos));
}

void Breadboard::removeActiveDevice() {
	if(m_menu_device_id.empty()) return;
	removeDevice(m_menu_device_id);
    m_menu_device_id = "";
}

void Breadboard::scaleActiveDevice() {
    auto device = m_devices.find(m_menu_device_id);
    if(device == m_devices.end()) {
        m_error_dialog->showMessage("Could not find device");
        return;
    }
    bool ok;
    int scale = QInputDialog::getInt(this, "Input new scale value", "Scale",
                                     device->second->getScale(), 1, 10, 1, &ok);
    if(ok && checkDevicePosition(device->second->getID(), device->second->getBuffer(),
                                 scale, getDistortedPosition(device->second->getBuffer().offset())).x()>=0) {
        device->second->setScale(scale);
    }
    m_menu_device_id = "";
}

void Breadboard::openDeviceConfigurations() {
    auto device = m_devices.find(m_menu_device_id);
    if(device == m_devices.end()) {
        m_error_dialog->showMessage("Could not find device");
        m_menu_device_id = "";
        return;
    }
    if(device->second->m_conf) m_device_configurations->setConfig(m_menu_device_id, device->second->m_conf->getConfig());
    else m_device_configurations->hideConfig();
    if(device->second->m_input) m_device_configurations->setKeys(m_menu_device_id, device->second->m_input->getKeys());
    else m_device_configurations->hideKeys();
    if(device->second->m_pin) m_device_configurations->setPins(m_menu_device_id, getPinsToDevicePins(m_menu_device_id));
    else m_device_configurations->hidePins();

    m_menu_device_id = "";
    m_device_configurations->exec();
}

void Breadboard::updateKeybinding(const DeviceID& device_id, Keys keys) {
	auto device = m_devices.find(device_id);
	if(device == m_devices.end() || !device->second->m_input) {
		m_error_dialog->showMessage("Device does not implement input interface.");
		return;
	}
	device->second->m_input->setKeys(keys);
}

void Breadboard::updateConfig(const DeviceID& device_id, Config config) {
	auto device = m_devices.find(device_id);
	if(device == m_devices.end() || !device->second->m_conf) {
		m_error_dialog->showMessage("Device does not implement config interface.");
		return;
	}
	device->second->m_conf->setConfig(config);
}

void Breadboard::updatePins(const DeviceID &device_id, unordered_map<Device::PIN_Interface::DevicePin, gpio::PinNumber> globals) {
    auto device = m_devices.find(device_id);
    if(device == m_devices.end() || !device->second->m_pin) {
        m_error_dialog->showMessage("Device does not implement pin interface.");
        return;
    }
    unordered_map<Device::PIN_Interface::DevicePin, gpio::PinNumber> current_globals = getPinsToDevicePins(device_id);
    for(auto const& [device_pin, global] : globals) {
        Row row = BB_ROWS;
        Device::PIN_Interface::DevicePin d_pin_save = device_pin;
        // get row for device_pin
        for(auto const& [device_pin_row, content] : m_raster) {
            auto exists = find_if(content.devices.begin(), content.devices.end(),
                                  [device_id, d_pin_save](const DeviceConnection& content_obj){
               return content_obj.id == device_id && content_obj.pin == d_pin_save;
            });
            if(exists != content.devices.end()) {
                row = device_pin_row;
                break;
            }
        }
        // if raster is not shown, new row number may have to be generated
        if(row == BB_ROWS) {
            if(isBreadboard()) return; // did not find device in raster, therefore can't add connection
            row = 0;
            if(m_raster.size() < std::numeric_limits<Row>::max()) {
                row = m_raster.size();
            }
            else {
                std::set<unsigned> used_row_numbers;
                for(auto const& [known_row, content] : m_raster) {
                    used_row_numbers.insert(known_row);
                }
                for(unsigned known_row : used_row_numbers) {
                    if(known_row > row)
                        break;
                    row++;
                }
            }
        }
        // get index
        auto row_content = m_raster.find(row);
        Index index = row_content != m_raster.end() ? row_content->second.devices.size() + row_content->second.pins.size() : 0;
        // remove old pin connection, if exists
        auto old_pin = current_globals.find(device_pin);
        if(old_pin != current_globals.end() && isHifivePin(old_pin->second)) {
            removePin(old_pin->second, false);
        }
        addPinToRow(row, index, global, "dialog");
    }

    printConnections();
}
