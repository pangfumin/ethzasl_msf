% plot_msf

        close all;
clear all;
clc;

data_file = '/home/pang/msf_meas.txt';
est_data_file = '/home/pang/msf_est.txt';
propagate_data_file = '/home/pang/msf_propagate.txt';

data = load(data_file);
est_data = load(est_data_file);
propagate_data = load(propagate_data_file);



position = data(:, 2:4);
propagate_position = propagate_data(:,2:4);
est_position = est_data(:,2:4);



tt = 1: size(est_position, 1);
t = 1: size(data, 1);

% figure(1);
% subplot(3,1,1)
% plot(t, position(:,1));
% subplot(3,1,2)
% plot(t, position(:,2));
% subplot(3,1,3)
% plot(t, position(:,3));
%
%
%

%
% figure(2);
% subplot(3,1,1)
% plot(tt, est_position(:,1));
% subplot(3,1,2)
% plot(tt, est_position(:,2));
% subplot(3,1,3)
% plot(tt, est_position(:,3));




figure(3);
plot3(position(:,1), position(:,2), position(:,3),'r');
hold on;
plot3(est_position(:,1), est_position(:,2), est_position(:,3),'g');
hold on;
plot3(propagate_position(:,1), propagate_position(:,2), propagate_position(:,3),'b');
axis equal;

% quaternion = data(:, 5:8);
% euler = quat2eul(quaternion);
% est_quaternion = est_data(:, 5:8);
% est_euler = quat2eul(est_quaternion);
% figure(4);
% subplot(3,1,1)
% plot(tt, est_euler(:,1), 'r', tt, euler(:,1),'g');
% subplot(3,1,2)
% plot(tt, est_euler(:,2), 'r', tt, euler(:,2),'g');
% subplot(3,1,3)
% plot(tt, est_euler(:,3), 'r', tt, euler(:,3),'g');