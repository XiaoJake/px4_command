/***************************************************************************************************************************
* px4_sender.cpp
*
* Author: Qyp
*
* Update Time: 2019.3.16
*
* Introduction:  PX4 command sender using px4 default command
*         1. Subscribe command.msg from upper nodes
*         2. Send command using command_to_mavros.h
*         3. Command includes:  (1)xyz+yaw
*                               (2)takeoff
*                               (3)land
*                               (4)idle
*                               (5)loiter
*                               (6)xyz+yaw(body frame)
***************************************************************************************************************************/

#include <ros/ros.h>

#include <command_to_mavros.h>
#include <px4_command/command.h>

#include <Eigen/Eigen>

using namespace std;
using namespace namespace_command_to_mavros;

//自定义的Command变量
//相应的命令分别为 待机 起飞 悬停 降落 移动(惯性系ENU) 上锁 移动(机体系)
//但目前 起飞和待机 并没有正式使用
enum Command
{
    Move_ENU,
    Move_Body,
    Hold,
    Land,
    Disarm,
    Failsafe_land,
    Idle
};

//Command Now [from upper node]
px4_command::command Command_Now;                      //无人机当前执行命令

//Command Last [from upper node]
px4_command::command Command_Last;                     //无人机上一条执行命令

float get_ros_time(ros::Time begin);
void prinft_command_state();

void Command_cb(const px4_command::command::ConstPtr& msg)
{
    Command_Now = *msg;
}
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>主 函 数<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
int main(int argc, char **argv)
{
    ros::init(argc, argv, "px4_sender");
    ros::NodeHandle nh("~");

    ros::Subscriber Command_sub = nh.subscribe<px4_command::command>("/px4/command", 10, Command_cb);

    // 频率 [50Hz]
    ros::Rate rate(50.0);

    Eigen::Vector3d pos_sp(0,0,0);

    Eigen::Vector3d vel_sp(0,0,0);

    command_to_mavros command_fsc;

    command_fsc.printf_param();

    command_fsc.show_geo_fence();

    int check_flag;
    // 这一步是为了程序运行前检查一下参数是否正确
    // 输入1,继续，其他，退出程序
    cout << "Please check the parameter and setting，1 for go on， else for quit: "<<endl;
    cin >> check_flag;

    if(check_flag != 1)
    {
        return -1;
    }

    // 等待和飞控的连接
    while(ros::ok() && command_fsc.current_state.connected)
    {
        ros::spinOnce();
        rate.sleep();
        ROS_INFO("Not Connected");
    }

    // 连接成功
    ROS_INFO("Connected!!");

    // 先读取一些飞控的数据
    int i =0;
    for(i=0;i<50;i++)
    {
        ros::spinOnce();
        rate.sleep();

    }


    command_fsc.set_takeoff_position();

    Command_Now.comid = 0;
    Command_Now.command = Idle;

    // 记录启控时间
    ros::Time begin_time = ros::Time::now();

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>主  循  环<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    while(ros::ok())
    {
        ros::spinOnce();


        float cur_time = get_ros_time(begin_time);

        command_fsc.prinft_drone_state2(cur_time);
        prinft_command_state();
        command_fsc.failsafe();

        //无人机一旦接受到Land指令，则会屏蔽其他指令
        if(Command_Last.command == Land)
        {
            Command_Now.command = Land;
        }

        switch (Command_Now.command)
        {
        case Move_ENU:

            if( Command_Now.sub_mode == 0 )
            {
                pos_sp = Eigen::Vector3d(Command_Now.pos_sp[0],Command_Now.pos_sp[1],Command_Now.pos_sp[2]);

                command_fsc.send_pos_setpoint(pos_sp, Command_Now.yaw_sp);
            }
            else if( Command_Now.sub_mode == 3 )
            {
                vel_sp = Eigen::Vector3d(Command_Now.vel_sp[0],Command_Now.vel_sp[1],Command_Now.vel_sp[2]);

                command_fsc.send_vel_setpoint(vel_sp, Command_Now.yaw_sp);
            }

            break;

        case Move_Body:

            vel_sp = Eigen::Vector3d(Command_Now.vel_sp[0],Command_Now.vel_sp[1],Command_Now.vel_sp[2]);

            command_fsc.send_vel_setpoint_body(vel_sp, Command_Now.yaw_sp);

            break;

        case Hold:

            command_fsc.loiter();

            break;


        case Land:

            command_fsc.land();

            break;

        case Disarm:

            if(command_fsc.current_state.mode == "OFFBOARD")
            {
                command_fsc.mode_cmd.request.custom_mode = "MANUAL";
                command_fsc.set_mode_client.call(command_fsc.mode_cmd);
            }

            if(command_fsc.current_state.armed)
            {
                command_fsc.arm_cmd.request.value = false;
                command_fsc.arming_client.call(command_fsc.arm_cmd);

            }

            if (command_fsc.arm_cmd.response.success)
            {
                cout<<"Disarm successfully!"<<endl;
            }

            break;

        case Failsafe_land:

            break;

        // 【】
        case Idle:
            command_fsc.idle();
            break;
        }


        Command_Last = Command_Now;

        rate.sleep();
    }

    return 0;

}

// 【获取当前时间函数】 单位：秒
float get_ros_time(ros::Time begin)
{
    ros::Time time_now = ros::Time::now();
    float currTimeSec = time_now.sec-begin.sec;
    float currTimenSec = time_now.nsec / 1e9 - begin.nsec / 1e9;
    return (currTimeSec + currTimenSec);
}
// 【打印控制指令函数】
void prinft_command_state()
{
    cout <<">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>Command State<<<<<<<<<<<<<<<<<<<<<<<<<<<<" <<endl;
    switch(Command_Now.command)
    {
    case Move_ENU:
        cout << "Command: [ Move_ENU ] " <<endl;
        break;
    case Move_Body:
        cout << "Command: [ Move_Body ] " <<endl;
        break;
    case Hold:
        cout << "Command: [ Hold ] " <<endl;
        break;
    case Land:
        cout << "Command: [ Land ] " <<endl;
        break;
    case Disarm:
        cout << "Command: [ Disarm ] " <<endl;
        break;
    case Failsafe_land:
        cout << "Command: [ Failsafe_land ] " <<endl;
        break;
    case Idle:
        cout << "Command: [ Idle ] " <<endl;
        break;

    }

    int sub_mode;
    sub_mode = Command_Now.sub_mode;

    if((sub_mode & 0b10) == 0) //xy channel
    {
        cout << "Submode: xy position control "<<endl;
        cout << "X_setpoint   : " << Command_Now.pos_sp[0] << " [ m ]"  << "  Y_setpoint : "<< Command_Now.pos_sp[1] << " [ m ]"<<endl;
    }
    else{
        cout << "Submode: xy velocity control "<<endl;
        cout << "X_setpoint   : " << Command_Now.vel_sp[0] << " [m/s]" << "  Y_setpoint : "<< Command_Now.vel_sp[1] << " [m/s]" <<endl;
    }

    if((sub_mode & 0b01) == 0) //z channel
    {
        cout << "Submode:  z position control "<<endl;
        cout << "Z_setpoint   : "<< Command_Now.pos_sp[2] << " [ m ]" << endl;
    }
    else
    {
        cout << "Submode:  z velocity control "<<endl;
        cout << "Z_setpoint   : "<< Command_Now.vel_sp[2] << " [m/s]" <<endl;
    }

    cout << "Yaw_setpoint : "  << Command_Now.yaw_sp << " [deg] " <<endl;
}
