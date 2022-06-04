#ifndef ENTITIES
#define ENTITIES

namespace obf{
    class positionEntity{
        public:
            int x;
            int y;
    };
    class updateEntity{
        public:
            virtual void update();
    };
    class Circle: public positionEntity, public updateEntity{
        public:
            sf::CircleShape shape;
            Circle(float radius);
            void update();
    };
}

#endif
