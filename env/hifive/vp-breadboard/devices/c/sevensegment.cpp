#include "sevensegment.h"

Sevensegment::Sevensegment(DeviceID id) : CDevice(id) {
	// Pin Layout
	if(!pin) {
		layout_pin = PinLayout();
		layout_pin.emplace(0, PinDesc{PinDesc::Dir::input, "top"});
		layout_pin.emplace(1, PinDesc{PinDesc::Dir::input, "top_right"});
		layout_pin.emplace(2, PinDesc{PinDesc::Dir::input, "bottom_right"});
		layout_pin.emplace(3, PinDesc{PinDesc::Dir::input, "bottom"});
		layout_pin.emplace(4, PinDesc{PinDesc::Dir::input, "bottom_left"});
		layout_pin.emplace(5, PinDesc{PinDesc::Dir::input, "top_left"});
		layout_pin.emplace(6, PinDesc{PinDesc::Dir::input, "center"});
		layout_pin.emplace(7, PinDesc{PinDesc::Dir::input, "dot"});
		pin = std::make_unique<Segment_PIN>(this);
	}
	// Graph Layout
	if(!graph) {
		layout_graph = Layout{36, 50, "rgba"};
		graph = std::make_unique<Segment_Graph>(this);
	}
}

Sevensegment::~Sevensegment() {}

const DeviceClass Sevensegment::getClass() const { return "sevensegment"; }

/* PIN Interface */

Sevensegment::Segment_PIN::Segment_PIN(CDevice* device) : CDevice::PIN_Interface_C(device) {}

void Sevensegment::Segment_PIN::setPin(PinNumber num, gpio::Tristate val) {
	if(num <= 7) {
		Sevensegment* segment_device = static_cast<Sevensegment*>(device);
		segment_device->draw_segment(num, val == gpio::Tristate::HIGH);
	}
}

/* Graphbuf Interface */

Sevensegment::Segment_Graph::Segment_Graph(CDevice* device) : CDevice::Graphbuf_Interface_C(device) {}

void Sevensegment::Segment_Graph::initializeBufferMaybe() {
	for(unsigned x=0; x<device->layout_graph.width; x++) {
		for(unsigned y=0; y<device->layout_graph.height; y++) {
			device->setBuffer(x, y, Pixel{0, 0, 0, 128});
		}
	}
	for(PinNumber num=0; num<=7; num++) {
		Sevensegment* segment_device = static_cast<Sevensegment*>(device);
		segment_device->draw_segment(num, false);
	}
}

void Sevensegment::draw_segment(PinNumber num, bool val) {
	if(num > 7) { return; }
	// general display stuff
	unsigned xcol1 = 0;
	unsigned xcol2 = 3 * (layout_graph.width / 4);
	unsigned yrow1 = 0;
	unsigned yrow2 = (layout_graph.height - 2) / 2;
	unsigned yrow3 = layout_graph.height - 2;
	unsigned x1=0, y1=0;
	unsigned x2=0, y2=0;
	// assign start and end point of line
	if(num == 0) {
		x1 = xcol1, y1 = yrow1;
		x2 = xcol2, y2 = yrow1;
	} else if(num == 1) {
		x1 = xcol2, y1 = yrow1;
		x2 = xcol2, y2 = yrow2;
	} else if(num == 2) {
		x1 = xcol2, y1 = yrow2;
		x2 = xcol2, y2 = yrow3;
	} else if(num == 3) {
		x1 = xcol1, y1 = yrow3;
		x2 = xcol2, y2 = yrow3;
	} else if(num == 4) {
		x1 = xcol1, y1 = yrow2;
		x1 = xcol1, y2 = yrow3;
	} else if(num == 5) {
		x1 = xcol1, y1 = yrow1;
		x2 = xcol1, y2 = yrow2;
	} else if(num == 6) {
		x1 = xcol1, y1 = yrow2;
		x2 = xcol2, y2 = yrow2;
	} else if(num == 7) {
		x1 = layout_graph.width - 4, y1 = yrow3 - 2;
		x2 = layout_graph.width - 2, y2 = yrow3;
	}
	for(unsigned x=x1; x<=x2+1; x++) {
		for(unsigned y=y1; y<=y2+1; y++) {
			setBuffer(x, y, Pixel{(val?(uint8_t)255:(uint8_t)0), 0, 0, 255});
		}
	}
}
