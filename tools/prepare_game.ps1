$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\examples\click_command.cpp'
$outPath = Join-Path $PSScriptRoot '..\examples\click_command.generated.cpp'
$src = Get-Content -LiteralPath $sourcePath -Raw

function Replace-Checked([string]$old, [string]$new, [string]$label) {
    if (-not $script:src.Contains($old)) {
        throw "prepare_game: no se encontro el bloque esperado: $label"
    }
    $script:src = $script:src.Replace($old, $new)
}

Replace-Checked `
'enum class Sheet{Grass=0,Plains,Decor,Fences,Walls,Flooring,Wooden,Chest,Carpet,Door,DoorB,Objects,Water,WaterDecor,WaterLillies,Count};' `
'enum class Sheet{Grass=0,Plains,Decor,Fences,Walls,Flooring,Wooden,Chest,Carpet,Door,DoorB,Objects,Water,WaterDecor,WaterLillies,Decor8,Chest2,RockSheet,RockSheetAlt,Rock1,Rock2,Rock3,Rock4,Rock5,Rock6,Water2,Water3,Water4,Water5,Water6,WaterSheet,Dust,Player,Slime,Skeleton,SkeletonSwordless,Count};' `
'Sheet enum'

$oldLoads = ' carpet_=loadOptional(e,"assets/mystic/tilesets/carpet.png");door_=loadOptional(e,"assets/mystic/tilesets/wooden_door.png");doorB_=loadOptional(e,"assets/mystic/tilesets/wooden_door_b.png");objects_=loadOptional(e,"assets/mystic/objects/objects.png");water_=loadOptional(e,"assets/mystic/tilesets/water1.png");waterDecor_=loadOptional(e,"assets/mystic/tilesets/water_decorations.png");waterLillies_=loadOptional(e,"assets/mystic/tilesets/water_lillies.png");'
$newLoads = @'
 carpet_=loadOptional(e,"assets/mystic/tilesets/floors/carpet.png");if(!carpet_)carpet_=loadOptional(e,"assets/mystic/tilesets/carpet.png");
 door_=loadOptional(e,"assets/mystic/tilesets/walls/wooden_door.png");if(!door_)door_=loadOptional(e,"assets/mystic/tilesets/wooden_door.png");
 doorB_=loadOptional(e,"assets/mystic/tilesets/walls/wooden_door_b.png");if(!doorB_)doorB_=loadOptional(e,"assets/mystic/tilesets/wooden_door_b.png");
 objects_=loadOptional(e,"assets/mystic/objects/objects.png");water_=loadOptional(e,"assets/mystic/tilesets/water1.png");waterDecor_=loadOptional(e,"assets/mystic/tilesets/water_decorations.png");waterLillies_=loadOptional(e,"assets/mystic/tilesets/water_lillies.png");
 decor8_=loadOptional(e,"assets/mystic/tilesets/decor_8x8.png");chest2_=loadOptional(e,"assets/mystic/objects/chest_02.png");rockSheet_=loadOptional(e,"assets/mystic/objects/rock_in_water-sheet.png");rockSheetAlt_=loadOptional(e,"assets/mystic/objects/rock_in_water_01-sheet.png");
 rock1_=loadOptional(e,"assets/mystic/objects/rock_in_water_01.png");rock2_=loadOptional(e,"assets/mystic/objects/rock_in_water_02.png");rock3_=loadOptional(e,"assets/mystic/objects/rock_in_water_03.png");rock4_=loadOptional(e,"assets/mystic/objects/rock_in_water_04.png");rock5_=loadOptional(e,"assets/mystic/objects/rock_in_water_05.png");rock6_=loadOptional(e,"assets/mystic/objects/rock_in_water_06.png");
 water2_=loadOptional(e,"assets/mystic/tilesets/water2.png");water3_=loadOptional(e,"assets/mystic/tilesets/water3.png");water4_=loadOptional(e,"assets/mystic/tilesets/water4.png");water5_=loadOptional(e,"assets/mystic/tilesets/water5.png");water6_=loadOptional(e,"assets/mystic/tilesets/water6.png");waterSheet_=loadOptional(e,"assets/mystic/tilesets/water-sheet.png");dust_=loadOptional(e,"assets/mystic/particles/dust_particles_01.png");
 skeleton_=loadOptional(e,"assets/mystic/characters/skeleton.png");skeletonSwordless_=loadOptional(e,"assets/mystic/characters/skeleton_swordless.png");
'@
Replace-Checked $oldLoads $newLoads.TrimEnd() 'asset loads'

$oldTex = 'const ra::Texture& tex(Sheet s)const{switch(s){case Sheet::Grass:return grass_;case Sheet::Plains:return plains_;case Sheet::Decor:return decor_;case Sheet::Fences:return fences_;case Sheet::Walls:return walls_;case Sheet::Flooring:return flooring_;case Sheet::Wooden:return wooden_;case Sheet::Chest:return chest_;case Sheet::Carpet:return carpet_;case Sheet::Door:return door_;case Sheet::DoorB:return doorB_;case Sheet::Objects:return objects_;case Sheet::Water:return water_;case Sheet::WaterDecor:return waterDecor_;case Sheet::WaterLillies:return waterLillies_;default:return grass_;}}'
$newTex = 'const ra::Texture& tex(Sheet s)const{switch(s){case Sheet::Grass:return grass_;case Sheet::Plains:return plains_;case Sheet::Decor:return decor_;case Sheet::Fences:return fences_;case Sheet::Walls:return walls_;case Sheet::Flooring:return flooring_;case Sheet::Wooden:return wooden_;case Sheet::Chest:return chest_;case Sheet::Carpet:return carpet_;case Sheet::Door:return door_;case Sheet::DoorB:return doorB_;case Sheet::Objects:return objects_;case Sheet::Water:return water_;case Sheet::WaterDecor:return waterDecor_;case Sheet::WaterLillies:return waterLillies_;case Sheet::Decor8:return decor8_;case Sheet::Chest2:return chest2_;case Sheet::RockSheet:return rockSheet_;case Sheet::RockSheetAlt:return rockSheetAlt_;case Sheet::Rock1:return rock1_;case Sheet::Rock2:return rock2_;case Sheet::Rock3:return rock3_;case Sheet::Rock4:return rock4_;case Sheet::Rock5:return rock5_;case Sheet::Rock6:return rock6_;case Sheet::Water2:return water2_;case Sheet::Water3:return water3_;case Sheet::Water4:return water4_;case Sheet::Water5:return water5_;case Sheet::Water6:return water6_;case Sheet::WaterSheet:return waterSheet_;case Sheet::Dust:return dust_;case Sheet::Player:return player_;case Sheet::Slime:return slime_;case Sheet::Skeleton:return skeleton_;case Sheet::SkeletonSwordless:return skeletonSwordless_;default:return grass_;}}'
Replace-Checked $oldTex $newTex 'texture switch'

$oldSheets = 'std::vector<SheetInfo> sheets()const{std::vector<SheetInfo>v={{Sheet::Grass,"GRASS",16,-1},{Sheet::Plains,"PLAINS",16,8},{Sheet::Decor,"DECOR 16x16",16,-1},{Sheet::Fences,"FENCES",16,-1},{Sheet::Walls,"WALLS",16,-1},{Sheet::Flooring,"FLOORING",16,-1},{Sheet::Wooden,"WOODEN FLOOR",16,-1},{Sheet::Chest,"CHEST",16,-1}};if(carpet_)v.push_back({Sheet::Carpet,"CARPET",16,-1});if(door_)v.push_back({Sheet::Door,"WOOD DOOR",16,-1});if(doorB_)v.push_back({Sheet::DoorB,"WOOD DOOR B",16,-1});if(objects_)v.push_back({Sheet::Objects,"OBJECTS",16,-1});if(water_)v.push_back({Sheet::Water,"WATER",16,-1});if(waterDecor_)v.push_back({Sheet::WaterDecor,"WATER DECOR",16,-1});if(waterLillies_)v.push_back({Sheet::WaterLillies,"WATER LILLIES",16,-1});return v;}'
$newSheets = 'std::vector<SheetInfo> sheets()const{std::vector<SheetInfo>v={{Sheet::Grass,"GRASS",16,-1},{Sheet::Plains,"PLAINS",16,8},{Sheet::Decor,"DECOR 16x16",16,-1},{Sheet::Fences,"FENCES",16,-1},{Sheet::Walls,"WALLS",16,-1},{Sheet::Flooring,"FLOORING",16,-1},{Sheet::Wooden,"WOODEN FLOOR",16,-1},{Sheet::Chest,"CHEST 1",16,-1}};auto add=[&](Sheet s,const char*n,int tile){if(tex(s))v.push_back({s,n,tile,-1});};add(Sheet::Carpet,"CARPET",16);add(Sheet::Door,"WOOD DOOR",16);add(Sheet::DoorB,"WOOD DOOR B",16);add(Sheet::Objects,"OBJECTS - COMPLETE",16);add(Sheet::Decor8,"DECOR 8x8",8);add(Sheet::Chest2,"CHEST 2",16);add(Sheet::RockSheet,"ROCK IN WATER SHEET",16);add(Sheet::RockSheetAlt,"ROCK IN WATER ALT",16);add(Sheet::Rock1,"ROCK WATER 1",16);add(Sheet::Rock2,"ROCK WATER 2",16);add(Sheet::Rock3,"ROCK WATER 3",16);add(Sheet::Rock4,"ROCK WATER 4",16);add(Sheet::Rock5,"ROCK WATER 5",16);add(Sheet::Rock6,"ROCK WATER 6",16);add(Sheet::Water,"WATER 1",16);add(Sheet::Water2,"WATER 2",16);add(Sheet::Water3,"WATER 3",16);add(Sheet::Water4,"WATER 4",16);add(Sheet::Water5,"WATER 5",16);add(Sheet::Water6,"WATER 6",16);add(Sheet::WaterSheet,"WATER COMPLETE SHEET",16);add(Sheet::WaterDecor,"WATER DECOR",16);add(Sheet::WaterLillies,"WATER LILLIES",16);add(Sheet::Dust,"DUST PARTICLES",12);add(Sheet::Player,"PLAYER FRAMES",48);add(Sheet::Slime,"SLIME FRAMES",32);add(Sheet::Skeleton,"SKELETON FRAMES",48);add(Sheet::SkeletonSwordless,"SKELETON SWORDLESS",48);return v;}'
Replace-Checked $oldSheets $newSheets 'sheet catalogue'

$oldTextures = 'ra::Texture grass_,plains_,decor_,fences_,walls_,flooring_,wooden_,chest_,player_,slime_,carpet_,door_,doorB_,objects_,water_,waterDecor_,waterLillies_;'
$newTextures = 'ra::Texture grass_,plains_,decor_,fences_,walls_,flooring_,wooden_,chest_,player_,slime_,carpet_,door_,doorB_,objects_,water_,waterDecor_,waterLillies_,decor8_,chest2_,rockSheet_,rockSheetAlt_,rock1_,rock2_,rock3_,rock4_,rock5_,rock6_,water2_,water3_,water4_,water5_,water6_,waterSheet_,dust_,skeleton_,skeletonSwordless_;'
Replace-Checked $oldTextures $newTextures 'texture members'

Set-Content -LiteralPath $outPath -Value $src -Encoding UTF8
Write-Host "Mystic Woods completo: fuente generada en examples\click_command.generated.cpp"
