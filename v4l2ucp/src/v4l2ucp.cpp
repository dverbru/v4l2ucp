/*  v4l2ucp - A universal control panel for all V4L2 devices
    Copyright (C) 2005,2009 Scott J. Bertin (scottbertin@yahoo.com)
    Copyright (C) 2009-2010 Vasily Khoruzhick (anarsoul@gmail.com)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 */

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <libv4l2.h>
#include <ros/ros.h>
#include <std_msgs/Empty.h>
#include <string>
#include <sys/ioctl.h>
#include <4l2ucp/v4l2ucp.h>

V4l2Ucp::V4l2Ucp() :
  fd(-1)
{
  ddr_ = std::make_unique<ddynamic_reconfigure::DDynamicReconfigure>();

  // TODO(lucasw) add device and open/closing it to ddr controls
  std::string device = "/dev/video0";
  ros::param::get("~device", device);

  fd = v4l2_open(device.c_str(), O_RDWR, 0);
  if (fd < 0)
  {
    ROS_ERROR_STREAM("v4l2ucp: Unable to open file" << device << " "
        << strerror(errno));
    return;
  }

  struct v4l2_capability cap;
  if (v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap) == -1)
  {
    ROS_ERROR_STREAM("v4l2ucp: Not a V4L2 device" << device);
    return;
  }

  ROS_INFO_STREAM(cap.driver);
  ROS_INFO_STREAM(cap.card);
  ROS_INFO_STREAM(cap.bus_info);

  ROS_INFO_STREAM((cap.version >> 16) << "." << ((cap.version >> 8) & 0xff) << "."
              << (cap.version & 0xff));

  ROS_INFO_STREAM("0x" << std::hex << cap.capabilities);

  struct v4l2_queryctrl ctrl;
#ifdef V4L2_CTRL_FLAG_NEXT_CTRL
  /* Try the extended control API first */
  ctrl.id = V4L2_CTRL_FLAG_NEXT_CTRL;
  if (0 == v4l2_ioctl(fd, VIDIOC_QUERYCTRL, &ctrl))
  {
    do
    {
      add_control(ctrl, fd);
      ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
    }
    while (0 == v4l2_ioctl(fd, VIDIOC_QUERYCTRL, &ctrl));
  }
  else
#endif
  {
    /* Fall back on the standard API */
    /* Check all the standard controls */
    for (int i = V4L2_CID_BASE; i < V4L2_CID_LASTP1; i++)
    {
      ctrl.id = i;
      if (v4l2_ioctl(fd, VIDIOC_QUERYCTRL, &ctrl) == 0)
      {
        add_control(ctrl, fd);
      }
    }

    /* Check any custom controls */
    for (int i = V4L2_CID_PRIVATE_BASE; ; i++)
    {
      ctrl.id = i;
      if (v4l2_ioctl(fd, VIDIOC_QUERYCTRL, &ctrl) == 0)
      {
        add_control(ctrl, fd);
      }
      else
      {
        break;
      }
    }
  }
  ddr_->publishServicesTopics();
}

V4l2Ucp::~V4l2Ucp()
{
  if (fd >= 0)
    v4l2_close(fd);
}

bool not_alnum(char s)
{
  return !std::isalnum(s);
}

void V4l2Ucp::add_control(const struct v4l2_queryctrl &ctrl, int fd)
{
  std::stringstream name_ss;
  name_ss << ctrl.name;
  std::string name = name_ss.str();
  // std::replace(name.begin(), name.end(), " ", "_");

  // http://stackoverflow.com/questions/6319872/how-to-strip-all-non-alphanumeric-characters-from-a-string-in-c
  // name.erase(std::remove_if(name.begin(), name.end(),
  // std::not1(std::ptr_fun(std::isalnum)), name.end()), name.end());
  name.erase(std::remove_if(name.begin(), name.end(),
      (int(*)(int))not_alnum), name.end());

  // TODO(lucasw) clear out all other params under controls first
  // a previous run would leave leftovers
#if 0
  ros::param::set("controls/" + name + "/name", name_ss.str());
  ros::param::set("controls/" + name + "/topic", "controls/" + name);
  ros::param::set("controls/" + name + "/min", ctrl.minimum);
  ros::param::set("controls/" + name + "/max", ctrl.maximum);
  ros::param::set("controls/" + name + "/default", ctrl.minimum);
#endif

  if (ctrl.flags & V4L2_CTRL_FLAG_DISABLED)
    return;

  switch (ctrl.type)
  {
  case V4L2_CTRL_TYPE_INTEGER:
    integer_controls_[name] = std::make_shared<V4L2IntegerControl>(fd, ctrl, this);
    // TODO(lucasw) how to set the value after init?
    ddr_.registerVariable(name, integer_controls_[name]->getValue(),
                          boost::bind(&V4l2Ucp::integerControlCallback, this, _1, name));
    break;
  case V4L2_CTRL_TYPE_BOOLEAN:
    bool_controls_[name] = std::make_shared<V4L2BooleanControl>(fd, ctrl, this);
    ddr_.registerVariable(name, integer_controls_[name]->getValue(),
                          boost::bind(&V4l2Ucp::boolControlCallback, this, _1, name));
    break;
  case V4L2_CTRL_TYPE_MENU:
    menu_controls_[name] = std::make_shared<V4L2MenuControl>(fd, ctrl, this);
    ddr_.registerVariable(name, integer_controls_[name]->getValue(),
                          boost::bind(&V4l2Ucp::menuControlCallback, this, _1, name));
    break;
  case V4L2_CTRL_TYPE_BUTTON:
    button_controls_[name] = std::make_shared<V4L2ButtonControl>(fd, ctrl, this);
    ddr_.registerVariable(name, integer_controls_[name]->getValue(),
                          boost::bind(&V4l2Ucp::buttonControlCallback, this, _1, name));
    break;
  case V4L2_CTRL_TYPE_INTEGER64:
    break;
  case V4L2_CTRL_TYPE_CTRL_CLASS:
    break;
  default:
    break;
  }


  #if 0
  if (!w)
  {
    if (ctrl.type == V4L2_CTRL_TYPE_CTRL_CLASS)
      layout->addWidget(new QLabel(parent));
    else
      layout->addWidget(new QLabel("Unknown control", parent));

    layout->addWidget(new QLabel(parent));
    layout->addWidget(new QLabel(parent));
    return;
  }
  #endif

  if (ctrl.flags & (V4L2_CTRL_FLAG_GRABBED | V4L2_CTRL_FLAG_READ_ONLY | V4L2_CTRL_FLAG_INACTIVE))
  {
    // w->setEnabled(false);
  }


  if (ctrl.type == V4L2_CTRL_TYPE_BUTTON)
  {
  }
  else
  {
  }

  // TODO(lucasw) have a ros timer that updates the values from hardware
}

void V4l2Ucp::integerControlCallback(
    int value, std::string name)
{
  // The ui is overriding this control immediately after it is set
  // need to set the slider to this value.
  // ROS_INFO_STREAM("integer " << name << " " << msg->data);
  integer_controls_[name]->setValue(value);
}

void V4l2Ucp::boolControlCallback(
    bool value, std::string name)
{
  bool_controls_[name]->setValue(value);
}

void V4l2Ucp::menuControlCallback(
    int value, std::string name)
{
  menu_controls_[name]->setValue(value);
}

void V4l2Ucp::buttonControlCallback(
    int value, std::string name)
{
  // TODO(lucasw) this doesn't do anything
  button_controls_[name]->setValue(value);
}

void V4l2Ucp::about()
{
  std::string about ="This application is a port of an original v4l2ucp to Qt4 library,\n"
                     "v4l2ucp is a universal control panel for all V4L2 devices. The\n"
                     "controls come directly from the driver. If they cause problems\n"
                     "with your hardware, please contact the maintainer of the driver.\n\n"
                     "Copyright (C) 2005 Scott J. Bertin\n"
                     "Copyright (C) 2009-2010 Vasily Khoruzhick\n\n"
                     "This program is free software; you can redistribute it and/or modify\n"
                     "it under the terms of the GNU General Public License as published by\n"
                     "the Free Software Foundation; either version 2 of the License, or\n"
                     "(at your option) any later version.\n\n"
                     "This program is distributed in the hope that it will be useful,\n"
                     "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                     "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
                     "GNU General Public License for more details.\n\n"
                     "You should have received a copy of the GNU General Public License\n"
                     "along with this program; if not, write to the Free Software\n"
                     "Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA\n";

  // ROS_INFO_STREAM("v4l2ucp Version " << V4L2UCP_VERSION << about);
}


int main(int argc, char **argv)
{
  ros::init(argc, argv, "v4l2ucp");
  V4l2Ucp main_window;
  ros::spin();
}
