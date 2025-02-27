/*****************************************************************************
 * Copyright (c) 2014-2019 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "../TrackImporter.h"
#include "../config/Config.h"
#include "../core/FileStream.hpp"
#include "../core/MemoryStream.h"
#include "../core/Path.hpp"
#include "../core/String.hpp"
#include "../rct12/SawyerChunkReader.h"
#include "../rct12/SawyerEncoding.h"
#include "../ride/Ride.h"
#include "../ride/TrackDesign.h"
#include "../ride/TrackDesignRepository.h"

/**
 * Class to import RollerCoaster Tycoon 2 track designs (*.TD6).
 */
class TD6Importer final : public ITrackImporter
{
private:
    MemoryStream _stream;
    std::string _name;

public:
    TD6Importer()
    {
    }

    bool Load(const utf8* path) override
    {
        const utf8* extension = Path::GetExtension(path);
        if (String::Equals(extension, ".td6", true))
        {
            _name = GetNameFromTrackPath(path);
            auto fs = FileStream(path, FILE_MODE_OPEN);
            return LoadFromStream(&fs);
        }
        else
        {
            throw std::runtime_error("Invalid RCT2 track extension.");
        }
    }

    bool LoadFromStream(IStream* stream) override
    {
        if (!gConfigGeneral.allow_loading_with_incorrect_checksum
            && SawyerEncoding::ValidateTrackChecksum(stream) != RCT12TrackDesignVersion::TD6)
        {
            throw IOException("Invalid checksum.");
        }

        auto chunkReader = SawyerChunkReader(stream);
        auto data = chunkReader.ReadChunkTrack();
        _stream.WriteArray<const uint8_t>(reinterpret_cast<const uint8_t*>(data->GetData()), data->GetLength());
        _stream.SetPosition(0);
        return true;
    }

    std::unique_ptr<TrackDesign> Import() override
    {
        std::unique_ptr<TrackDesign> td = std::make_unique<TrackDesign>();

        rct_track_td6 td6{};
        // Rework td6 so that it is just the fields
        _stream.Read(&td6, 0xA3);

        td->type = td6.type; // 0x00
        td->vehicle_type = td6.vehicle_type;

        td->cost = 0;
        td->flags = td6.flags;
        td->ride_mode = td6.ride_mode;
        td->track_flags = 0;
        td->colour_scheme = td6.version_and_colour_scheme & 0x3;
        for (auto i = 0; i < RCT2_MAX_CARS_PER_TRAIN; ++i)
        {
            td->vehicle_colours[i] = td6.vehicle_colours[i];
            td->vehicle_additional_colour[i] = td6.vehicle_additional_colour[i];
        }
        td->entrance_style = td6.entrance_style;
        td->total_air_time = td6.total_air_time;
        td->depart_flags = td6.depart_flags;
        td->number_of_trains = td6.number_of_trains;
        td->number_of_cars_per_train = td6.number_of_cars_per_train;
        td->min_waiting_time = td6.min_waiting_time;
        td->max_waiting_time = td6.max_waiting_time;
        td->operation_setting = td6.operation_setting;
        td->max_speed = td6.max_speed;
        td->average_speed = td6.average_speed;
        td->ride_length = td6.ride_length;
        td->max_positive_vertical_g = td6.max_positive_vertical_g;
        td->max_negative_vertical_g = td6.max_negative_vertical_g;
        td->max_lateral_g = td6.max_lateral_g;

        if (td->type == RIDE_TYPE_MINI_GOLF)
        {
            td->holes = td6.holes;
        }
        else
        {
            td->inversions = td6.inversions;
        }

        td->drops = td6.drops;
        td->highest_drop_height = td6.highest_drop_height;
        td->excitement = td6.excitement;
        td->intensity = td6.intensity;
        td->nausea = td6.nausea;
        td->upkeep_cost = td6.upkeep_cost;
        for (auto i = 0; i < RCT12_NUM_COLOUR_SCHEMES; ++i)
        {
            td->track_spine_colour[i] = td6.track_spine_colour[i];
            td->track_rail_colour[i] = td6.track_rail_colour[i];
            td->track_support_colour[i] = td6.track_support_colour[i];
        }
        td->flags2 = td6.flags2;
        td->vehicle_object = td6.vehicle_object;
        td->space_required_x = td6.space_required_x;
        td->space_required_y = td6.space_required_y;
        td->lift_hill_speed = td6.lift_hill_speed_num_circuits & 0b00011111;
        td->num_circuits = td6.lift_hill_speed_num_circuits >> 5;

        auto version = static_cast<RCT12TrackDesignVersion>((td6.version_and_colour_scheme >> 2) & 3);
        if (version != RCT12TrackDesignVersion::TD6)
        {
            log_error("Unsupported track design.");
            return nullptr;
        }

        td->operation_setting = std::min(td->operation_setting, RideProperties[td->type].max_value);

        if (td->type == RIDE_TYPE_MAZE)
        {
            rct_td46_maze_element t6MazeElement{};
            t6MazeElement.all = !0;
            while (t6MazeElement.all != 0)
            {
                _stream.Read(&t6MazeElement, sizeof(rct_td46_maze_element));
                if (t6MazeElement.all != 0)
                {
                    TrackDesignMazeElement mazeElement{};
                    mazeElement.x = t6MazeElement.x;
                    mazeElement.y = t6MazeElement.y;
                    mazeElement.direction = t6MazeElement.direction;
                    mazeElement.type = t6MazeElement.type;
                    td->maze_elements.push_back(mazeElement);
                }
            }
        }
        else
        {
            rct_td46_track_element t6TrackElement{};
            for (uint8_t endFlag = _stream.ReadValue<uint8_t>(); endFlag != 0xFF; endFlag = _stream.ReadValue<uint8_t>())
            {
                _stream.SetPosition(_stream.GetPosition() - 1);
                _stream.Read(&t6TrackElement, sizeof(rct_td46_track_element));
                TrackDesignTrackElement trackElement{};
                trackElement.type = t6TrackElement.type;
                trackElement.flags = t6TrackElement.flags;
                td->track_elements.push_back(trackElement);
            }

            rct_td6_entrance_element t6EntranceElement{};
            for (uint8_t endFlag = _stream.ReadValue<uint8_t>(); endFlag != 0xFF; endFlag = _stream.ReadValue<uint8_t>())
            {
                _stream.SetPosition(_stream.GetPosition() - 1);
                _stream.Read(&t6EntranceElement, sizeof(rct_td6_entrance_element));
                TrackDesignEntranceElement entranceElement{};
                entranceElement.z = t6EntranceElement.z;
                entranceElement.direction = t6EntranceElement.direction & 0x7F;
                entranceElement.x = t6EntranceElement.x;
                entranceElement.y = t6EntranceElement.y;
                entranceElement.isExit = t6EntranceElement.direction >> 7;
                td->entrance_elements.push_back(entranceElement);
            }
        }

        for (uint8_t endFlag = _stream.ReadValue<uint8_t>(); endFlag != 0xFF; endFlag = _stream.ReadValue<uint8_t>())
        {
            _stream.SetPosition(_stream.GetPosition() - 1);
            rct_td6_scenery_element t6SceneryElement{};
            _stream.Read(&t6SceneryElement, sizeof(rct_td6_scenery_element));
            TrackDesignSceneryElement sceneryElement{};
            sceneryElement.scenery_object = t6SceneryElement.scenery_object;
            sceneryElement.x = t6SceneryElement.x;
            sceneryElement.y = t6SceneryElement.y;
            sceneryElement.z = t6SceneryElement.z;
            sceneryElement.flags = t6SceneryElement.flags;
            sceneryElement.primary_colour = t6SceneryElement.primary_colour;
            sceneryElement.secondary_colour = t6SceneryElement.secondary_colour;
            td->scenery_elements.push_back(sceneryElement);
        }

        td->name = _name;
        return td;
    }
};

std::unique_ptr<ITrackImporter> TrackImporter::CreateTD6()
{
    return std::make_unique<TD6Importer>();
}
