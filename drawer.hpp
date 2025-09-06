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
    ScreenSpaceSizer::Size get_current_lod() const { return current_size; }

    unsigned int ivp_x_id;
    bool cached_at_least_once = false;

    draw_info::BufferModificationTracker buffer_modification_tracker;
    vertex_geometry::AxisAlignedBoundingBox aabb;
    draw_info::IndexedVertexPositions ivp_aabb;
    draw_info::IVPColor lod_debug_rect;

    // NOTE: the idea is if an object has not been drawn in a "long" time then we can remove it from the gpu buffer
    // so we take up less storage.
    Timer stop_drawing_timer{1};

    Efficient(int ivp_x_id, std::function<IVPX()> get_high_lod_ivp, std::function<IVPX()> get_medium_lod_ivp,
              std::function<IVPX()> get_low_lod_ivp,
              ScreenSpaceSizer::Size initial_size = ScreenSpaceSizer::Size::Large)
        : ivp_x_id(ivp_x_id) {

        // Store generator functions
        lod_generators[ScreenSpaceSizer::Size::Large] = std::move(get_high_lod_ivp);
        lod_generators[ScreenSpaceSizer::Size::Medium] = std::move(get_medium_lod_ivp);
        lod_generators[ScreenSpaceSizer::Size::Small] = std::move(get_low_lod_ivp);

        current_size = initial_size;

        current_ivpx_generator_for_lod_level = lod_generators[ScreenSpaceSizer::Size::Large];

        // NOTE: using it here once.
        IVPX ivp_x = current_ivpx_generator_for_lod_level();
        aabb = vertex_geometry::AxisAlignedBoundingBox(ivp_x.xyz_positions);
    }

    void just_drew_object() { stop_drawing_timer.start(); }

    bool hasnt_been_seen_in_a_while() { return stop_drawing_timer.time_up(); }

    // void update_aabb_geometry() {
    //     vertex_geometry::AxisAlignedBoundingBox aabb(ivp_x.xyz_positions);
    //     auto new_aabb = aabb.get_ivp();
    //     ivp_aabb.xyz_positions = new_aabb.xyz_positions;
    //     ivp_aabb.indices = new_aabb.indices;
    //
    //     ivp_aabb.transform = ivp_x.transform;
    //     ivp_aabb.buffer_modification_tracker.just_modified();
    // }

    void update_lod_debug_rect_geometry(ScreenSpaceSizer::Size new_ll) {
        PROFILE_SECTION();

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

        // auto new_lod_debug_rect = draw_info::IVPColor(screen_space_sizer.make_screen_space_ivp(ivp_x), color);
        // lod_debug_rect.xyz_positions = new_lod_debug_rect.xyz_positions;
        // lod_debug_rect.indices = new_lod_debug_rect.indices;
        // lod_debug_rect.rgb_colors = new_lod_debug_rect.rgb_colors;
        // lod_debug_rect.buffer_modification_tracker.just_modified();
    }

    void optionally_cache_ivp_x_into_batcher_based_on_lod(ScreenSpaceSizer::Size new_size, bool override = false) {
        LogSection _(global_logger, "update_ivp_geometry_based_on_lod");

        bool same_size_as_before = new_size == current_size;
        if (not override && same_size_as_before) {
            return; // no change in lod so no update required
        }

        // otherwise lod changed, so updated required.
        auto it = lod_generators.find(new_size);
        if (it != lod_generators.end()) {
            auto new_ivp = it->second();

            new_ivp.id = ivp_x_id;
            // cache that object here and replace. or just do caching outside, caching can take in a full object as
            // well, need cache that takes in ivps

            // this is about to get cut out. instead we just upload the new geometry using the cache function
            // auto old_id = ivp_x.id;
            // auto old_buffer_modification_tracker = ivp_x.buffer_modification_tracker;
            //
            // ivp_x = new_ivp;
            // ivp_x.id = old_id;
            // ivp_x.buffer_modification_tracker = old_buffer_modification_tracker;
            //
            // ivp_x.buffer_modification_tracker.just_modified();

            // probably not required doing it anyways.
            // update_aabb_geometry();
        } else {
            std::cout << "couldn't find associated lod generator" << std::endl;
        }
        current_size = new_size;
    }

    bool screen_size_just_changed(ScreenSpaceSizer &screen_space_sizer) {
        LogSection _(global_logger, "update");
        // dummy because I don't store the transform right now and use absolute instead for everything.
        Transform t;
        auto new_size = screen_space_sizer.get_screen_size(aabb, t);
        bool size_just_changed = current_size != new_size;
        if (size_just_changed) {
            // cheap.
            current_ivpx_generator_for_lod_level = lod_generators[new_size];
            // NOTE: this is "theoretically" true in a deferred sense.
            buffer_modification_tracker.just_modified();
        }
        current_size = new_size;
        return size_just_changed;
    }

    // NOTE: this is only called when a size change occurs.
    IVPX get_current_ivp_x_for_lod_level() {
        auto ivp_x = current_ivpx_generator_for_lod_level();
        ivp_x.id = ivp_x_id;
        // NOTE: we need to give this ivp the correct buffer history as it's new it won't have that.
        ivp_x.buffer_modification_tracker = buffer_modification_tracker;
        return ivp_x;
    }

    ScreenSpaceSizer::Size current_size = ScreenSpaceSizer::Size::Large;

  private:
    std::function<IVPX()> current_ivpx_generator_for_lod_level;
    std::unordered_map<ScreenSpaceSizer::Size, std::function<IVPX()>> lod_generators;
};

// TODO: add more when you need more.
using EfficientIVP = Efficient<draw_info::IndexedVertexPositions>;
using EfficientIVPColor = Efficient<draw_info::IVPColor>;

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

    Logger logger{"drawer"};

  private:
    template <typename T> struct Entry {
        T obj;
        int draw_id;
        int delete_id;
    };

  public:
    Drawer(
        ICamera &cam, const unsigned int &screen_width_px, const unsigned int &screen_height_px,
        DrawerSettings settings = {},
        IVPCDrawFn screen_space_bounding_box_drawing_fun = [](draw_info::IVPColor &ivpc) {})
        : camera(cam), screen_space_sizer(cam, screen_width_px, screen_height_px),

          screen_space_bounding_box_drawing_fun(screen_space_bounding_box_drawing_fun), settings(settings) {};

    IVPCDrawFn screen_space_bounding_box_drawing_fun;

    ICamera &camera;

    ScreenSpaceSizer screen_space_sizer;

    DrawerSettings settings;

    [[nodiscard]] int register_draw_function(IVPDrawFunc fn) {
        int id = next_ivp_id++;
        ivp_draw_functions[id] = fn;
        return id;
    }

    // NOTE: the delete function is on the grpahics card only clearing it off there.
    [[nodiscard]] int register_delete_function(IVPDrawFunc fn) {
        int id = next_ivp_delete_id++;
        ivp_delete_functions[id] = fn;
        return id;
    }

    // need to do the main.py hierarchy for cache.
    [[nodiscard]] int register_cache_function(IVPCDrawFn fn) {
        int id = next_ivpc_cache_id++;
        ivpc_cache_functions[id] = fn;
        return id;
    }

    [[nodiscard]] int register_draw_function(std::function<void(unsigned int &)> fn) {
        int id = next_ivpc_id++;
        ivpc_draw_functions[id] = fn;
        return id;
    }

    [[nodiscard]] int register_delete_function(IVPCDrawFn fn) {
        int id = next_ivpc_delete_id++;
        ivpc_delete_functions[id] = fn;
        return id;
    }

    // TODO: disgusting duplication because I don't have a better way yet, try to fix when we're really mad at it
    void add_object(const int object_id, const draw_info::IndexedVertexPositions ivp, int draw_func_id,
                    int delete_func_id) {
        auto high_lod_ivp_getter = [=]() { return ivp; };
        auto medium_lod_ivp_getter = [=]() { return ivp; };
        auto low_lod_ivp_getter = [=]() {
            auto aabb = vertex_geometry::AxisAlignedBoundingBox(ivp.xyz_positions);
            auto aabb_ivp = aabb.get_ivp();
            aabb_ivp.transform = ivp.transform;
            return aabb_ivp;
        };

        EfficientIVP e_ivp(object_id, high_lod_ivp_getter, medium_lod_ivp_getter, low_lod_ivp_getter);
        e_ivps.push_back({e_ivp, draw_func_id, delete_func_id});
    }

    void add_object(const int object_id, const draw_info::IVPColor ivpc, int draw_func_id, int delete_func_id) {
        auto high_lod_ivp_getter = [=]() { return ivpc; };
        auto medium_lod_ivp_getter = [=]() { return ivpc; };
        auto low_lod_ivp_getter = [=]() {
            // TODO: remove this computation.
            auto aabb = vertex_geometry::AxisAlignedBoundingBox(ivpc.xyz_positions);
            auto aabb_ivp = aabb.get_ivp();
            // TODO: bad assumption that the ivp has at least one color so we can copy paste it in.
            auto aabb_ivpc = draw_info::IVPColor(aabb_ivp, ivpc.rgb_colors[0]);
            aabb_ivpc.transform = ivpc.transform;
            return aabb_ivpc;
        };

        EfficientIVPColor e_ivpc(object_id, high_lod_ivp_getter, medium_lod_ivp_getter, low_lod_ivp_getter);
        e_ivpcs.push_back({e_ivpc, draw_func_id, delete_func_id});
    }

    void add_object(const int object_id, std::function<draw_info::IVPColor()> ivpc_gen_fun, int draw_func_id,
                    int delete_func_id) {
        LogSection _(global_logger, "add_object");
        // I Don't want to call it everytime? can I use memoization?
        auto high_lod_ivp_getter = [=]() { return ivpc_gen_fun(); };
        auto medium_lod_ivp_getter = [=]() { return ivpc_gen_fun(); };
        auto low_lod_ivp_getter = [=]() {
            // TODO: remove this computation.
            auto ivpc = ivpc_gen_fun(); // NOTE: make sure this sets the transform as well I guess.
            auto aabb = vertex_geometry::AxisAlignedBoundingBox(ivpc.xyz_positions);
            auto aabb_ivp = aabb.get_ivp();
            // TODO: bad assumption that the ivp has at least one color so we can copy paste it in.
            auto aabb_ivpc = draw_info::IVPColor(aabb_ivp, ivpc.rgb_colors[0]);
            aabb_ivpc.transform = ivpc.transform;
            return aabb_ivpc;
        };

        EfficientIVPColor e_ivpc(object_id, high_lod_ivp_getter, medium_lod_ivp_getter, low_lod_ivp_getter);
        e_ivpcs.push_back({e_ivpc, draw_func_id, delete_func_id});
    }

    // TODO: for some reason passing these lambdas not by reference makes it not work, so I'm just not going to question
    // that because I can't figure out why that would make a difference... I hate templating once again.
    template <draw_info::IVPLike IVPX>
    void efficiently_draw_e_ivp_x(Efficient<IVPX> &e_ivp_x, std::function<void(unsigned int &)> &draw_fun,
                                  std::function<void(IVPX &)> &cache_fun, std::function<void(IVPX &)> &delete_fun) {

        PROFILE_SECTION();

        LogSection _(global_logger, "efficiently_draw_e_ivp_x");

        // e_ivp_x.update();

        if (settings.use_frustrum_culling) {
            LogSection _(global_logger, "frustrum culling");

            Transform t;

            if (camera.is_visible(e_ivp_x.aabb, t)) {

                // draw_fun(e_ivp_x.ivp_x);

                if (settings.show_debug_data) {
                    // can't use same draw fun here.
                    // draw_fun(e_ivp_x.ivp_aabb);
                    screen_space_bounding_box_drawing_fun(e_ivp_x.lod_debug_rect);
                }

                e_ivp_x.just_drew_object();
            } else { // Not visible

                // PROFILE_SECTION("not visible");
                // bool can_be_removed = e_ivp_x.hasnt_been_seen_in_a_while() and
                //                       e_ivp_x.ivp_x.buffer_modification_tracker.has_data_in_buffer();

                // if (can_be_removed) {
                // delete_fun(e_ivp_x.ivp_x);
                // delete_fun(e_ivp_x.ivp_aabb);
                // }
            }
        } else {
            PROFILE_SECTION("regular drawing");
            LogSection _(global_logger, "regular drawing");
            Transform t;
            {
                PROFILE_SECTION("is visible");
                if (not camera.is_visible(e_ivp_x.aabb, t))
                    return;
            }

            {
                PROFILE_SECTION("too small check");
                bool too_small = screen_space_sizer.smaller_than_pixel(e_ivp_x.aabb, t);
                if (too_small)
                    return;
            }

            {

                PROFILE_SECTION("update geom");

                bool not_yet_uploaded_geom = not e_ivp_x.buffer_modification_tracker.has_data_in_buffer();

                bool need_to_upload_geom =
                    not_yet_uploaded_geom or (e_ivp_x.screen_size_just_changed(screen_space_sizer));

                if (need_to_upload_geom) {
                    // expensive call
                    IVPX ivp_x = e_ivp_x.get_current_ivp_x_for_lod_level();
                    cache_fun(ivp_x);
                    e_ivp_x.buffer_modification_tracker.just_buffered_data();
                }
            }

            {
                PROFILE_SECTION("regular drawing");
                draw_fun(e_ivp_x.ivp_x_id);
            }
            e_ivp_x.just_drew_object();
        }

        // st.report();
    }

    void draw_all() {
        PROFILE_SECTION();
        LogSection _(global_logger, "draw all");

        for (auto &[e_ivp, draw_id, delete_id] : e_ivps) {
            auto draw_fun = ivp_draw_functions[draw_id];
            auto cache_fun = ivpc_cache_functions[draw_id];
            auto delete_fun = ivp_delete_functions[delete_id];
            // efficiently_draw_e_ivp_x(e_ivp, draw_fun, delete_fun);
        }

        for (auto &[e_ivpc, draw_id, delete_id] : e_ivpcs) {
            // TODO: fix id usage
            std::function<void(unsigned int &)> draw_fun = ivpc_draw_functions[draw_id];
            IVPCDrawFn cache_fun = ivpc_cache_functions[draw_id];
            IVPCDrawFn delete_fun = ivpc_delete_functions[delete_id];
            // draw_fun(e_ivpc.ivp_x);
            efficiently_draw_e_ivp_x(e_ivpc, draw_fun, cache_fun, delete_fun);
        }
    }

    std::vector<Entry<EfficientIVP>> e_ivps;
    std::vector<Entry<EfficientIVPColor>> e_ivpcs;

  private:
    // make objects out of these to simplify in the future

    // Object storage

    // Function registries
    int next_ivp_id = 0;
    int next_ivp_delete_id = 0;

    int next_ivpc_id = 0;
    int next_ivpc_cache_id = 0;
    int next_ivpc_delete_id = 0;

    std::unordered_map<int, IVPDrawFunc> ivp_draw_functions;
    std::unordered_map<int, IVPDrawFunc> ivp_delete_functions;

    std::unordered_map<int, std::function<void(unsigned int &)>> ivpc_draw_functions;
    std::unordered_map<int, IVPCDrawFn> ivpc_cache_functions;
    std::unordered_map<int, IVPCDrawFn> ivpc_delete_functions;
};
#endif // DRAWER
