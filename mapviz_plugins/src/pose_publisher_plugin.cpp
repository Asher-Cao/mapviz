// *****************************************************************************
//
// Copyright (c) 2021, Trinity University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of Southwest Research Institute® (SwRI®) nor the
//       names of its contributors may be used to endorse or promote products
//       derived from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// *****************************************************************************

#include <mapviz_plugins/pose_publisher_plugin.h>

// C++ standard libraries
#include <cstdio>
#include <vector>

// QT libraries
#include <QDateTime>
#include <QDialog>
#include <QGLWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStaticText>
#include <QDebug>
#include <QSettings>
#include <fstream>

// ROS libraries
#include <ros/master.h>
#include <swri_transform_util/frames.h>

// Declare plugin
#include <pluginlib/class_list_macros.h>

PLUGINLIB_EXPORT_CLASS(mapviz_plugins::PosePublisherPlugin, mapviz::MapvizPlugin)

namespace mapviz_plugins
{

PosePublisherPlugin::PosePublisherPlugin() :
    config_widget_(new QWidget()),
    map_canvas_(NULL),
    is_mouse_down_(false),
    monitoring_action_state_(false)
{
    // TODO - make pose a field
    //pose_pub_ = nh_.advertise<geometry_msgs::PoseWithCovarianceStamped>("/selected_pose", 1, false);

    ui_.setupUi(config_widget_);

    // Set background white
    QPalette p(config_widget_->palette());
    p.setColor(QPalette::Background, Qt::white);
    config_widget_->setPalette(p);
    // Set status text red

    ui_.status->setText("OK");
    QPalette p3(ui_.status->palette());
    p3.setColor(QPalette::Text, Qt::green);
    ui_.status->setPalette(p3);

    QObject::connect(ui_.pushButtonPose, SIGNAL(toggled()),
                     this, SLOT(on_pushButtonPose_toggled()));

    QObject::connect(ui_.topic, SIGNAL(textEdited(const QString&)),
            this, SLOT(topicChanged(const QString&)));

    timer_ = nh_.createTimer(ros::Duration(1.0), &PosePublisherPlugin::timerCallback, this);
    frame_timer_.start(1000);
    QObject::connect(&frame_timer_, SIGNAL(timeout()), this, SLOT(updateFrames()));
}

PosePublisherPlugin::~PosePublisherPlugin()
{
    if (map_canvas_)
    {
        map_canvas_->removeEventFilter(this);
    }
}

void PosePublisherPlugin::PrintError(const std::string& message)
{
    PrintErrorHelper( ui_.status, message);
}

void PosePublisherPlugin::PrintInfo(const std::string& message)
{
    PrintInfoHelper( ui_.status, message);
}

void PosePublisherPlugin::PrintWarning(const std::string& message)
{
    PrintWarningHelper( ui_.status, message);
}

QWidget* PosePublisherPlugin::GetConfigWidget(QWidget* parent)
{
    config_widget_->setParent(parent);
    return config_widget_;
}

bool PosePublisherPlugin::Initialize(QGLWidget* canvas)
{
    map_canvas_ = static_cast<mapviz::MapCanvas*>(canvas);
    map_canvas_->installEventFilter(this);
    initialized_ = true;
    return true;
}

bool PosePublisherPlugin::eventFilter(QObject *object, QEvent* event)
{
    switch (event->type())
    {
    case QEvent::MouseButtonPress:
        return handleMousePress(static_cast<QMouseEvent*>(event));
    case QEvent::MouseButtonRelease:
        return handleMouseRelease(static_cast<QMouseEvent*>(event));
    case QEvent::MouseMove:
        return handleMouseMove(static_cast<QMouseEvent*>(event));
    default:
        return false;
    }
}

void PosePublisherPlugin::timerCallback(const ros::TimerEvent &)
{
  ui_.pushButtonPose->setEnabled( true );
  PrintInfoHelper( ui_.status, "OK");
}


bool PosePublisherPlugin::handleMousePress(QMouseEvent* event)
{
    bool pose_checked = ui_.pushButtonPose->isChecked();

    if( !pose_checked)
    {
        return false;
    }

    if (event->button() == Qt::LeftButton)
    {
        is_mouse_down_ = true;
        arrow_angle_ = 0;
#if QT_VERSION >= 0x050000
      arrow_tail_position_= map_canvas_->MapGlCoordToFixedFrame( event->localPos() );
#else
      arrow_tail_position_= map_canvas_->MapGlCoordToFixedFrame( event->posF() );
#endif
        return true;
    }
    return false;
}

bool PosePublisherPlugin::handleMouseMove(QMouseEvent* event)
{
    if (is_mouse_down_)
    {
#if QT_VERSION >= 0x050000
        QPointF head_pos = map_canvas_->MapGlCoordToFixedFrame( event->localPos() );
#else
        QPointF head_pos = map_canvas_->MapGlCoordToFixedFrame( event->posF() );
#endif
        arrow_angle_ = atan2( head_pos.y() - arrow_tail_position_.y(),
                              head_pos.x() - arrow_tail_position_.x() );
    }
    return false;
}

bool PosePublisherPlugin::handleMouseRelease(QMouseEvent* event)
{
    if( !is_mouse_down_ )
    {
        return false;
    }

    is_mouse_down_ = false;

    bool pose_checked = ui_.pushButtonPose->isChecked();
    if( !pose_checked )
    {
        return false;
    }

    tf::Quaternion quat = tf::createQuaternionFromYaw(arrow_angle_);

    if( pose_checked )
    {
        geometry_msgs::PoseWithCovarianceStamped pose;
        pose.header.frame_id = target_frame_;
        pose.header.stamp = ros::Time::now();
        pose.pose.pose.position.x = arrow_tail_position_.x();
        pose.pose.pose.position.y = arrow_tail_position_.y();
        pose.pose.pose.position.z = 0.0;
        tf::quaternionTFToMsg( quat, pose.pose.pose.orientation );

        pose_pub_.publish(pose);
        ui_.pushButtonPose->setChecked(false);

        std::stringstream ss;
        ss << "Pose published to to topic: " <<  ui_.topic->text().toStdString().c_str()
           << "in frame " << target_frame_;
        PrintInfo(ss.str());
        PrintInfoHelper( ui_.status, ss.str());
    }
    return true;
}


void PosePublisherPlugin::Draw(double x, double y, double scale)
{
    std::array<QPointF, 7> arrow_points;
    arrow_points[0] = QPointF(10, 0);
    arrow_points[1] = QPointF(6, -2.5);
    arrow_points[2] = QPointF(6.5, -1);
    arrow_points[3] = QPointF(0, -1);
    arrow_points[4] = QPointF(0, 1);
    arrow_points[5] = QPointF(6.5, 1);
    arrow_points[6] = QPointF(6, 2.5);

    if( is_mouse_down_ )
    {
        QPointF transformed_points[7];
        for (size_t i=0; i<7; i++ )
        {
            tf::Vector3 point(arrow_points[i].x(), arrow_points[i].y(), 0);
            point *= scale*10;
            point = tf::Transform( tf::createQuaternionFromYaw(arrow_angle_)  ) * point;
            transformed_points[i] = QPointF(point.x() + arrow_tail_position_.x(),
                                            point.y() + arrow_tail_position_.y() );
        }
        glColor3f(0.1, 0.9, 0.1);
        glLineWidth(2);
        glBegin(GL_TRIANGLE_FAN);
        for (const QPointF& point: transformed_points )
        {
            glVertex2d(point.x(), point.y());
        }
        glEnd();

        glColor3f(0.0, 0.6, 0.0);
        glBegin(GL_LINE_LOOP);
        for (const QPointF& point: transformed_points )
        {
            glVertex2d(point.x(), point.y());
        }
        glEnd();
    }
}


void PosePublisherPlugin::LoadConfig(const YAML::Node& node, const std::string& path)
{
    std::string tmp;
    if (swri_yaml_util::FindValue(node, "topic"))
    {
      node["topic"] >> tmp;
      ui_.topic->setText(QString(tmp.c_str()));
      topicChanged(ui_.topic->text());
    }

    if (swri_yaml_util::FindValue(node, "output_frame"))
    {
      node["output_frame"] >> tmp;
      ui_.outputframe->addItem(QString(tmp.c_str()));
    }

}

void PosePublisherPlugin::SaveConfig(YAML::Emitter& emitter, const std::string& path)
{
    emitter << YAML::Key << "topic" << YAML::Value << ui_.topic->text().toStdString();
    emitter << YAML::Key << "output_frame" << YAML::Value << ui_.outputframe->currentText().toStdString();
}

void PosePublisherPlugin::on_pushButtonPose_toggled(bool checked)
{
    if(checked)
    {
      QPixmap cursor_pixmap = QPixmap(":/images/green-arrow.png");
      QApplication::setOverrideCursor(QCursor(cursor_pixmap));
    }
    else
    {
      QApplication::restoreOverrideCursor();
    }
}



  void PosePublisherPlugin::topicChanged(const QString& topic)
  {
    std::stringstream ss;
    ss << "Publishing points to topic: " << topic.toStdString().c_str();
    PrintInfo(ss.str());

    if (!topic.isEmpty())
    {
      pose_pub_ = node_.advertise<geometry_msgs::PoseWithCovarianceStamped>(topic.toStdString(), 1000);
    }
  }

  void PosePublisherPlugin::updateFrames()
  {
    std::vector<std::string> frames;
    tf_->getFrameStrings(frames);

    std::stringstream ss;
    ss << "tf_ knows about: ";
    for (auto f: frames)
    {
      ss << f << ", ";
    }
    PrintInfo(ss.str());

    bool supports_wgs84 = tf_manager_->SupportsTransform(
        swri_transform_util::_local_xy_frame,
        swri_transform_util::_wgs84_frame);

    if (supports_wgs84)
    {
      frames.push_back(swri_transform_util::_wgs84_frame);
    }

    ss.str(std::string()); // clear string
    ss << "possibly including wgs84: ";
    for (auto f: frames)
    {
      ss << f << ", ";
    }
    PrintInfo(ss.str());

    if (ui_.outputframe->count() >= 0 &&
        static_cast<size_t>(ui_.outputframe->count()) == frames.size())
    {
      bool changed = false;
      for (size_t i = 0; i < frames.size(); i++)
      {
        if (frames[i] != ui_.outputframe->itemText(static_cast<int>(i)).toStdString())
        {
          changed = true;
        }
      }

      if (!changed)
        PrintInfo("frame not changed");
        return;
    }

    std::string current_output = ui_.outputframe->currentText().toStdString();

    ss.str(std::string()); // clear string
    ss << "current_output = " << current_output;
    PrintInfo(ss.str());

    ui_.outputframe->clear();
    for (size_t i = 0; i < frames.size(); i++)
    {
      ui_.outputframe->addItem(frames[i].c_str());
      ss.str(std::string()); // clear string
      ss << "adding " << frames[i];
      PrintInfo(ss.str());
    }

    if (current_output != "")
    {
      int index = ui_.outputframe->findText(current_output.c_str());
      if (index < 0)
      {
        ui_.outputframe->addItem(current_output.c_str());
      }

      index = ui_.outputframe->findText(current_output.c_str());
      ui_.outputframe->setCurrentIndex(index);
      ss.str(std::string()); // clear string
      ss << "setting frame to " << current_output;
      PrintInfo(ss.str());
    }
    else
    {
    PrintInfo("current output is blank...");
    }
  }

}
