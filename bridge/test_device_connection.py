import unittest
from types import SimpleNamespace
from device_connection import select_port

def port(name, location, serial='', vid=0x1a86, pid=0x7523):
    return SimpleNamespace(device=name,location=location,serial_number=serial,vid=vid,pid=pid)

class DeviceSelectionTests(unittest.TestCase):
    def test_absent_device_waits(self):
        self.assertIsNone(select_port([], 'COM10'))

    def test_changed_com_keeps_usb_identity(self):
        device=port('COM14','1-4.3.1')
        other=port('COM10','1-8')
        saved={'vid':0x1a86,'pid':0x7523,'location':'1-4.3.1'}
        self.assertIs(select_port([other,device],'COM10',saved),device)

    def test_ambiguous_devices_do_not_use_recycled_com(self):
        self.assertIsNone(select_port([port('COM10','1-1'),port('COM12','1-2')], 'COM10'))

    def test_serial_number_does_not_fall_back_to_wrong_device(self):
        self.assertIsNone(select_port([port('COM10','1-1','another')], 'COM10',
                                     {'vid':0x1a86,'pid':0x7523,'serial':'paired'}))

    def test_rp2040_never_selected(self):
        self.assertIsNone(select_port([port('COM11',None,vid=0x2e8a,pid=10)], 'COM11'))

    def test_unique_matching_device_can_be_probed_after_move(self):
        moved=port('COM15','1-5')
        self.assertIs(select_port([moved],'COM10',{'location':'1-4'}),moved)

if __name__=='__main__': unittest.main()
