#include <stdio.h>
#include <libudev.h>


int main(int argc, char *argv[])
{
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;

	udev = udev_new();
	enumerate = udev_enumerate_new(udev);
	devices =  udev_enumerate_add_match_subsystem(enumerate, "power_supply");
	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *path;
		path = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, path);
		//power supply wouldn't have a node
		printf("Device node path %s\n", udev_device_get_devnode(dev));
		udev_device_get_parent_with_subsystem_devtype(dev, "power_supply", NULL);
///		udev_monitor_filter_add_match_sub
	}



	udev_enumerate_unref(enumerate);
	udev_unref(udev);
	return 0;
}
