%% Set extents of bounding box around 3D objectr

% model_3d = pcread('/home/paloma/catkin_ws/rpgig_ws/src/rpg_ig_active_reconstruction/example/flying_gazebo_stereo_cam/model/pier_block_support/meshes/model.ply')
% pos_lb = [-30 -20 0];
% pos_ub =  [20 146 50];
pos_lb = [-1 -1 0];
pos_ub =  [1 1 1];

%%

num_views = 48;
dir_viewspace = '/home/paloma/catkin_ws/rpgig_ws/src/rpg_ig_active_reconstruction/example/flying_gazebo_stereo_cam/config/pier';

filename = strcat(dir_viewspace, '/dome_', num2str(num_views), '_views.txt');
dlmwrite(filename, num_views);

view_idx = 1;
viewspace_matrix = zeros(num_views, 7);

while view_idx <= num_views
    
    % sample (x,y,z) position
    pos = pos_lb + rand(1,3).*(pos_ub-pos_lb);
    if norm(pos(1:2)) < 0.5 % too close to object
        continue;
    end
    
    % sample (q0,q1,q2,q3) orientation
    dir1 = [0 0 1];
    dir2 = -normr([pos(1) pos(2) 0]);
    R = rot_vec( dir1, dir2 );
    quat = quaternion.rotationmatrix(R);
    
    viewspace_matrix(view_idx, :) = [pos vector(quat)' real(quat)];
    view_idx = view_idx + 1;
end

dlmwrite(filename, viewspace_matrix, '-append', 'delimiter', ' ');