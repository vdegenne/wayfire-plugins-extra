#include <wayfire/output.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>

#include <map>
#include <memory>
#include <set>

namespace wf {
namespace background_window {

const std::string transformer_name = "background-window";

class background_view_transformer : public wf::scene::view_2d_transformer_t {
public:
  background_view_transformer(wayfire_view view)
      : wf::scene::view_2d_transformer_t(view) {}

  std::optional<wf::scene::input_node_t>
  find_node_at(const wf::pointf_t &at) override {
    // Completely ignore pointer/touch input.
    return {};
  }
};

class always_on_bottom_root_node_t : public wf::scene::output_node_t {
public:
  using output_node_t::output_node_t;

  std::string stringify() const override {
    return "background-window always-on-bottom for output " +
           get_output()->to_string() + " " + stringify_flags();
  }
};

class background_plugin : public wf::plugin_interface_t {
  wf::ipc_activator_t background_toggle{"background-window/background_toggle"};

  std::set<wayfire_view> background_views;

  std::map<wayfire_view, std::shared_ptr<background_view_transformer>>
      transformers;

  std::map<wf::output_t *, wf::scene::floating_inner_ptr> always_below;

  void ensure_bottom_layer(wf::output_t *output) {
    if (!output || always_below.count(output)) {
      return;
    }

    auto node = std::make_shared<always_on_bottom_root_node_t>(output);

    wf::scene::add_back(
        wf::get_core().scene()->layers[(int)wf::scene::layer::WORKSPACE], node);

    always_below[output] = node;
  }

  void set_always_on_bottom(wayfire_view view) {
    if (!view) {
      return;
    }

    auto output = view->get_output();

    if (!output) {
      return;
    }

    ensure_bottom_layer(output);

    wf::scene::readd_front(always_below[output], view->get_root_node());
  }

  void add_transformer(wayfire_view view) {
    if (!view) {
      return;
    }

    if (transformers.count(view)) {
      return;
    }

    auto transformer = std::make_shared<background_view_transformer>(view);

    view->get_transformed_node()->add_transformer(
        transformer, wf::TRANSFORMER_2D, transformer_name);

    transformers[view] = transformer;
  }

  void remove_transformer(wayfire_view view) {
    if (!view) {
      return;
    }

    view->get_transformed_node()->rem_transformer(transformer_name);

    transformers.erase(view);
  }

  void bury_view(wayfire_view view) {
    if (!view) {
      return;
    }

    if (background_views.count(view)) {
      return;
    }

    auto toplevel = wf::toplevel_cast(view);

    if (!toplevel) {
      return;
    }

    auto output = view->get_output();

    if (!output) {
      return;
    }

    background_views.insert(view);

    // Make the window sticky.
    toplevel->set_sticky(true);

    // Make it completely ignore pointer/touch input.
    add_transformer(view);

    // Put it underneath normal workspace windows.
    set_always_on_bottom(view);
  }

  void remove_background_state(wayfire_view view) {
    if (!view) {
      return;
    }

    if (!background_views.count(view)) {
      return;
    }

    remove_transformer(view);

    background_views.erase(view);

    auto output = view->get_output();

    if (output) {
      wf::scene::readd_front(output->wset()->get_node(), view->get_root_node());
    }
  }

  /*
   * Swallow keyboard events before they reach the client whenever
   * the destination node belongs to one of our background windows.
   */
  wf::signal::connection_t<
      wf::pre_client_input_event_signal<wlr_keyboard_key_event>>
      keyboard_connection =
          [this](
              wf::pre_client_input_event_signal<wlr_keyboard_key_event> *ev) {
            if (!ev->focus_node) {
              return;
            }

            auto view = wf::node_to_view(ev->focus_node);

            if (view && background_views.count(view)) {
              ev->carried_out = true;
            }
          };

  wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped_connection =
      [this](wf::view_unmapped_signal *ev) {
        if (!ev->view) {
          return;
        }

        remove_background_state(ev->view);
      };

  wf::signal::connection_t<wf::view_set_output_signal>
      view_set_output_connection = [this](wf::view_set_output_signal *ev) {
        if (!ev->view) {
          return;
        }

        if (!background_views.count(ev->view)) {
          return;
        }

        /*
         * set_output() removes the view from the old scene layer,
         * so put it into the bottom layer of the new output.
         */
        set_always_on_bottom(ev->view);
      };

public:
  void init() override {
    background_toggle.set_handler(
        [this](wf::output_t *output, wayfire_view view) {
          if (view) {
            bury_view(view);
          }

          return true;
        });

    wf::get_core().connect(&keyboard_connection);
    wf::get_core().connect(&view_unmapped_connection);
    wf::get_core().connect(&view_set_output_connection);
  }

  void fini() override {
    /*
     * Remove the click-through transformers.
     */
    for (auto &view : background_views) {
      remove_transformer(view);

      if (view) {
        auto output = view->get_output();

        if (output) {
          wf::scene::readd_front(output->wset()->get_node(),
                                 view->get_root_node());
        }
      }
    }

    background_views.clear();

    /*
     * Remove our artificial bottom-layer nodes.
     */
    for (auto &[output, node] : always_below) {
      if (node) {
        wf::scene::remove_child(node);
      }
    }

    always_below.clear();
  }
};

} // namespace background_window
} // namespace wf

DECLARE_WAYFIRE_PLUGIN(wf::background_window::background_plugin);
