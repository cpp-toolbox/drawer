#ifndef DRAWER
#define DRAWER

#include <vector>

#include "sbpt_generated_includes.hpp"

#include <vector>
#include <unordered_map>
#include <functional>
#include <iostream>

// NOTE: we templated this because we don't want to have to re-write the same code again for each type, but maybe
// there's a better way of doing this.
template <draw_info::IVPLike IVPX> class Efficient {
  public:
    ScreenSpaceSizer::Size get_current_lod() const { return current_lod; }

    // NOTE: this is the actual underlying geometry, the thing we want to draw
    // and I believe that this can be templatized?
    IVPX ivp_x;

    draw_info::IndexedVertexPositions ivp_aabb;
    draw_info::IVPColor lod_debug_rect;

    // NOTE: the idea is if an object has not been drawn in a "long" time then we can remove it from the gpu buffer
    // so we take up less storage.
    Timer stop_drawing_timer{1};

    Efficient(ScreenSpaceSizer &lod_selector, std::function<IVPX()> get_high_lod_ivp,
              std::function<IVPX()> get_medium_lod_ivp, std::function<IVPX()> get_low_lod_ivp,
              ScreenSpaceSizer::Size initial_lod = ScreenSpaceSizer::Size::Large)
        : lod_selector(lod_selector) {
        // Store generator functions
        lod_generators[ScreenSpaceSizer::Size::Large] = std::move(get_high_lod_ivp);
        lod_generators[ScreenSpaceSizer::Size::Medium] = std::move(get_medium_lod_ivp);
        lod_generators[ScreenSpaceSizer::Size::Small] = std::move(get_low_lod_ivp);

        current_lod = initial_lod;
        update_ivp_geometry_based_on_lod(current_lod, true);
    }

    void just_drew_object() { stop_drawing_timer.start(); }

    bool hasnt_been_seen_in_a_while() { return stop_drawing_timer.time_up(); }

    void update_aabb_geometry() {
        vertex_geometry::AxisAlignedBoundingBox aabb(ivp_x.xyz_positions);
        auto new_aabb = aabb.get_ivp();
        ivp_aabb.xyz_positions = new_aabb.xyz_positions;
        ivp_aabb.indices = new_aabb.indices;

        ivp_aabb.transform = ivp_x.transform;
        ivp_aabb.buffer_modification_tracker.just_modified();
    }

    void update_lod_debug_rect_geometry(ScreenSpaceSizer::Size new_ll) {
        glm::vec3 color;
        switch (new_ll) {
        case ScreenSpaceSizer::Size::Large:
            color = colors::green;
            break;

        case ScreenSpaceSizer::Size::Medium:
            color = colors::yellow;
            break;

        case ScreenSpaceSizer::Size::Small:
            color = colors::red;
            break;
        }

        auto new_lod_debug_rect = draw_info::IVPColor(lod_selector.make_screen_space_ivp(ivp_x), color);
        lod_debug_rect.xyz_positions = new_lod_debug_rect.xyz_positions;
        lod_debug_rect.indices = new_lod_debug_rect.indices;
        lod_debug_rect.rgb_colors = new_lod_debug_rect.rgb_colors;
        lod_debug_rect.buffer_modification_tracker.just_modified();
    }

    void update_ivp_geometry_based_on_lod(ScreenSpaceSizer::Size new_ll, bool override = false) {
        if (not override && new_ll == current_lod) {
            return;
        }

        auto it = lod_generators.find(new_ll);
        if (it != lod_generators.end()) {
            auto new_ivp = it->second();
            // TODO: in the future this needs to be genearlized to whatever type we're using...
            ivp_x.xyz_positions = new_ivp.xyz_positions;
            ivp_x.indices = new_ivp.indices;
            ivp_x.buffer_modification_tracker.just_modified();
            ivp_x.transform = new_ivp.transform;

            // probably not required doing it anyways.
            update_aabb_geometry();
        }
        current_lod = new_ll;
    }

    // updates the lod rectangle, and the actual geometry if its small etc...
    // NOTE: needs to be called every frame, or at least some cyclic rate
    void update() {
        auto lod_level = lod_selector.get_screen_size(ivp_x.xyz_positions, ivp_x.transform);
        update_lod_debug_rect_geometry(lod_level);
        update_ivp_geometry_based_on_lod(lod_level);
    }

  private:
    ScreenSpaceSizer &lod_selector;

    ScreenSpaceSizer::Size current_lod = ScreenSpaceSizer::Size::Large;
    std::unordered_map<ScreenSpaceSizer::Size, std::function<IVPX()>> lod_generators;
};

// TODO: add more when you need more.
using EfficientIVP = Efficient<draw_info::IndexedVertexPositions>;

struct DrawerSettings {
    bool show_debug_data = true;
    bool remove_objects_from_buffer_if_not_visible_for_while = true;
    bool use_frustrum_culling = true;
};

class Drawer {

    using IVPDrawFunc = std::function<void(draw_info::IndexedVertexPositions &)>;
    // TODO: the below have to be updated eventually
    using IVPCDrawFn = std::function<void(draw_info::IVPColor &)>;
    using IVPNCDrawFn = std::function<void(draw_info::IVPNColor &)>;

  public:
    Drawer(
        FPSCamera &cam, const unsigned int &screen_width_px, const unsigned int &screen_height_px,
        DrawerSettings settings = {},
        IVPCDrawFn screen_space_bounding_box_drawing_fun = [](draw_info::IVPColor &ivpc) {})
        : screen_space_sizer(cam, screen_width_px, screen_height_px),
          frustrum_culler(cam, screen_width_px, screen_height_px),
          screen_space_bounding_box_drawing_fun(screen_space_bounding_box_drawing_fun), settings(settings) {};

    IVPCDrawFn screen_space_bounding_box_drawing_fun;

    ScreenSpaceSizer screen_space_sizer;
    FrustumCuller frustrum_culler;

    DrawerSettings settings;

    [[nodiscard]] int register_draw_function(IVPDrawFunc fn) {
        int id = next_ivp_id++;
        ivp_draw_functions[id] = fn;
        return id;
    }

    // NOTE: the delete function is on the grpahics card only clearing it off there.
    [[nodiscard]] int register_delete_function(IVPDrawFunc fn) {
        int id = next_ivp_id++;
        ivp_delete_functions[id] = fn;
        return id;
    }

    int register_draw_function(IVPCDrawFn fn) {
        int id = next_ivpc_id++;
        ivpc_draw_functions[id] = fn;
        return id;
    }

    int register_draw_function(IVPNCDrawFn fn) {
        int id = next_ivpnc_id++;
        ivpnc_draw_functions[id] = fn;
        return id;
    }

    void add_object(const draw_info::IndexedVertexPositions ivp, int draw_func_id, int delete_func_id) {
        auto high_lod_ivp_getter = [=]() { return ivp; };
        auto medium_lod_ivp_getter = [=]() { return ivp; };
        auto low_lod_ivp_getter = [=]() {
            auto aabb = vertex_geometry::AxisAlignedBoundingBox(ivp.xyz_positions);
            auto aabb_ivp = aabb.get_ivp();
            aabb_ivp.transform = ivp.transform;
            return aabb_ivp;
        };

        EfficientIVP e_ivp(screen_space_sizer, high_lod_ivp_getter, medium_lod_ivp_getter, low_lod_ivp_getter);
        e_ivps.push_back({e_ivp, draw_func_id, delete_func_id});
    }
    void add_object(const draw_info::IVPColor &obj, int draw_func_id) { ivpcs.push_back({obj, draw_func_id}); }
    void add_object(const draw_info::IVPNColor &obj, int draw_func_id) { ivpncs.push_back({obj, draw_func_id}); }

    void draw_all() {
        for (auto &[e_ivp, draw_id, delete_id] : e_ivps) {
            e_ivp.update();
            auto draw_fun = ivp_draw_functions[draw_id];
            auto delete_fun = ivp_delete_functions[delete_id];

            if (settings.use_frustrum_culling) {
                if (frustrum_culler.is_visible(e_ivp.ivp_x)) {
                    draw_fun(e_ivp.ivp_x);

                    if (settings.show_debug_data) {
                        draw_fun(e_ivp.ivp_aabb);
                        screen_space_bounding_box_drawing_fun(e_ivp.lod_debug_rect);
                    }

                    // TODO screen space rects

                    e_ivp.just_drew_object();
                } else { // NOTE: means not visible.
                    bool can_be_removed = e_ivp.hasnt_been_seen_in_a_while() and
                                          e_ivp.ivp_x.buffer_modification_tracker.has_data_in_buffer();

                    if (can_be_removed) {
                        delete_fun(e_ivp.ivp_x);
                        delete_fun(e_ivp.ivp_aabb);
                    }
                }
            } else {
                draw_fun(e_ivp.ivp_x);
                e_ivp.just_drew_object();
            }
        }
        // for (auto &[obj, draw_id] : ivpcs) {
        //     ivpc_draw_functions[draw_id](obj);
        // }
        // for (auto &[obj, draw_id] : ivpncs) {
        //     ivpnc_draw_functions[draw_id](obj);
        // }
    }

  private:
    template <typename T> struct Entry {
        T obj;
        int draw_id;
        int delete_id;
    };

    // Object storage
    std::vector<Entry<EfficientIVP>> e_ivps;
    std::vector<Entry<draw_info::IVPColor>> ivpcs;
    std::vector<Entry<draw_info::IVPNColor>> ivpncs;

    // Function registries
    int next_ivp_id = 0;
    int next_ivp_delete_id = 0;
    int next_ivpc_id = 0;
    int next_ivpnc_id = 0;

    std::unordered_map<int, IVPDrawFunc> ivp_draw_functions;
    std::unordered_map<int, IVPDrawFunc> ivp_delete_functions;
    std::unordered_map<int, IVPCDrawFn> ivpc_draw_functions;
    std::unordered_map<int, IVPNCDrawFn> ivpnc_draw_functions;
};
#endif // DRAWER
