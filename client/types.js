// Ported from include/types.hpp. Plain objects (not namespaces).

export const Packets = {
  Ping: 0,
  CreateEntity: 1,
  SyncEntity: 2,
  Nickname: 3,
  Controls: 4,
  AssignEntity: 5,
  DeleteEntity: 6,
  ColorEntity: 7,
  Chat: 8,
  PingInfo: 9,
  ResizeView: 10,
  Name: 11,
  PlanetCollision: 12,
  SyncDone: 13,
  SetTarget: 14,
  FullClear: 15,
  VarChange: 16,
};

export const Entities = {
  Triangle: 1,
  CelestialBody: 2,
  Projectile: 3,
  Missile: 4,
};

export const Types = {
  String: 0,
  Int8: 1,
  Int32: 2,
  Double: 3,
  Bool: 6,
};
