#include "PathTileSet.h"

int32 UPathTileSet::IndexOfTile(FName Id) const
{
	return Tiles.IndexOfByPredicate([Id](const FPathTileDef& Def) { return Def.Id == Id; });
}
