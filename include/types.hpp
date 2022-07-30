#pragma once

// use like an enum e.g. Packets::CreateEntity
namespace obf::Packets {

constexpr uint16_t Ping = 0,
	CreateEntity = 1,
	SyncEntity = 2,
	Nickname = 3,
	Controls = 4,
	AssignEntity = 5,
	DeleteEntity = 6,
	ColorEntity = 7,
	Chat = 8,
	PingInfo = 9,
	ResizeView = 10,
	Name = 11,
	PlanetCollision = 12,
	SyncDone = 13;
}

namespace obf::Entities {

constexpr uint8_t Triangle = 1,
 	Attractor = 2,
	Projectile = 3;
}

namespace obf::Types {

constexpr uint8_t String = 0,
	Short_u = 1,
	Int = 2,
	Double = 3,
	Bool = 4;
}
