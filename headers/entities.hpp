#pragma once

namespace obf{
    class positionEntity{
        public:
            double x;
            double y;
            void setPosition(double, double);
    };
    class updateEntity{
        public:
            virtual void update();
    };
    class Circle: public positionEntity, public updateEntity{
        public:
            sf::CircleShape shape;
            Circle(double, double, double);
            void setPosition(double, double);
            void update();
    };
}
