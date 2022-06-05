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
    class posVelEntity: public positionEntity, public updateEntity{
        public:
            double velX;
            double velY;
            void setVelocity(double, double);
            void addVelocity(double, double);
    };
    class Circle: public posVelEntity{
        public:
            sf::CircleShape shape;
            Circle(double, double, double);
            void setPosition(double, double);
            void update();
    };
}
