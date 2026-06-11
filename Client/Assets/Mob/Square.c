#include <Client/Assets/Render.h>
// Copyright (C) 2026 Lazur

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.

// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <Client/Renderer/Renderer.h>

void rr_square_draw(struct rr_renderer *renderer)
{
    rr_renderer_set_fill(renderer, 0xfff7d259);
    rr_renderer_set_stroke(renderer, 0xffc7a334);
    rr_renderer_set_line_width(renderer, 9.0f);
    rr_renderer_begin_path(renderer);

    const float size_big = 100.0f;
    const float radius_square = 15.0f;
    
    float x = -size_big / 2.0f;
    float y = -size_big / 2.0f;

    rr_renderer_move_to(renderer, x + radius_square, y);
    
    rr_renderer_line_to(renderer, x + size_big - radius_square, y);
    rr_renderer_bezier_curve_to(renderer, x + size_big, y, x + size_big, y + radius_square, x + size_big, y + radius_square);

    rr_renderer_line_to(renderer, x + size_big, y + size_big - radius_square);
    rr_renderer_bezier_curve_to(renderer, x + size_big, y + size_big, x + size_big - radius_square, y + size_big, x + size_big - radius_square, y + size_big);

    rr_renderer_line_to(renderer, x + radius_square, y + size_big);
    rr_renderer_bezier_curve_to(renderer, x, y + size_big, x, y + size_big - radius_square, x, y + size_big - radius_square);

    rr_renderer_line_to(renderer, x, y + radius_square);
    rr_renderer_bezier_curve_to(renderer, x, y, x + radius_square, y, x + radius_square, y);

    rr_renderer_fill(renderer);
    rr_renderer_stroke(renderer);


}