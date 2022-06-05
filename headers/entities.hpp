#pragma once

namespace obf{
    class positionEntity{
        public:
            double x;
            double y;
    };
    class updateEntity{
        public:
            virtual void update();
    };
    class Circle: public positionEntity, public updateEntity{
        public:
            sf::CircleShape shape;
            Circle(double radius);
            void update();
    };
}
