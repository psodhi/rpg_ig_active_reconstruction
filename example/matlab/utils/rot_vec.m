function R = rot_vec( a, b )
%UNTITLED2 Summary of this function goes here
%   Detailed explanation goes here

v = cross(a,b);
s = norm(v);
c = dot(a,b);
w = [0 -v(3) v(2); v(3) 0 -v(1); -v(2) v(1) 0];
R = eye(3) + w + w*w*(1-c)/(s^2);

end