load('nodes_x.mat')
load('nodes_y.mat')
load 'hop_snapshot';
load 'ch_snapshot';
load 'energy_snapshot';
figure;

snap = 7500;

axis([0, AREA_SIZE_X, 0, AREA_SIZE_Y]);
%scatter(nodes_x, nodes_y, '+');
hold on;
scatter(SINK_X, SINK_Y, '*');

hops = hop_snapshot(:,snap);
chs = ch_snapshot(:,snap);

for i = 2:NODE_NUM
    if (energy_snapshot(i, snap) < 0)
        scatter(nodes_x(i), nodes_y(i), '*', 'k');
        circle(nodes_x(i), nodes_y(i), 1);
    elseif (energy_snapshot(i, snap) < 500)
        scatter(nodes_x(i), nodes_y(i), '+', 'r');
    elseif (energy_snapshot(i, snap) < 1000)
        scatter(nodes_x(i), nodes_y(i), '+', 'y');
    elseif (energy_snapshot(i, snap) < 1500)
        scatter(nodes_x(i), nodes_y(i), '+', 'g');
    elseif (energy_snapshot(i, snap) <= 2000)
        scatter(nodes_x(i), nodes_y(i), '+', 'b');
    end
    if (energy_snapshot(hops(i)+1, snap) > 0 || hops(i) == SINK_ADDR) && energy_snapshot(i, snap) > 0
        if chs(i) == i-1
            plot([nodes_x(i), nodes_x(hops(i)+1)], [nodes_y(i), nodes_y(hops(i)+1)], 'r','linewidth',2);
            circle(nodes_x(chs(i)+1), nodes_y(chs(i)+1), 1);
            circle(nodes_x(chs(i)+1), nodes_y(chs(i)+1), 1.3);
        else
            plot([nodes_x(i), nodes_x(hops(i)+1)], [nodes_y(i), nodes_y(hops(i)+1)]);
        end
    end
end
axis([0, AREA_SIZE_X, 0, AREA_SIZE_Y]);