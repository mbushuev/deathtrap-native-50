"""Regression tests for atlas extraction and rigid-spine registration."""
import unittest
import statistics
from PIL import Image, ImageDraw
from pack_menu_book import align_cells, component_cells, spine_anchor, rigid_base_cells, affine_between, map_point, resample_registered


class BookPackingTests(unittest.TestCase):
    def test_changing_cover_and_shadows_do_not_move_support(self):
        cells=[]
        for dx,dy,top,shadow in [(0,0,28,0),(4,7,17,7),(-3,12,2,13),(5,-4,10,2)]:
            image=Image.new("RGBA",(110,130))
            draw=ImageDraw.Draw(image)
            draw.rectangle((18+dx,40+dy,37+dx,92+dy),fill=(70,35,20,255))
            draw.rectangle((38+dx,top+dy,81+dx,91+dy),fill=(190,160,110,255))
            if shadow:
                draw.rectangle((50+dx,92+dy,81+dx,92+dy+shadow),fill=(15,10,5,64))
            cells.append(image)
        _,report=align_cells(cells,124,134)
        for axis in [0,1]:
            positions=[frame["mapped_anchor"][axis] for frame in report["frames"]]
            self.assertLessEqual(max(positions)-min(positions),1.0)

    def test_uneven_atlas_does_not_cut_tall_book_at_grid_boundary(self):
        atlas=Image.new("RGBA",(140,140))
        draw=ImageDraw.Draw(atlas)
        for x in [10,85]:
            draw.rectangle((x,4,x+25,73),fill=(90,40,20,255))
            draw.rectangle((x,88,x+25,135),fill=(90,40,20,255))
        cells=component_cells(atlas,2,2)
        self.assertEqual(len(cells),4)
        self.assertGreater(cells[0].height,70)
        self.assertGreater(cells[1].height,70)
        self.assertLess(cells[2].height,60)
        self.assertLess(cells[3].height,60)

    def test_transparent_frames_and_small_canvas_fail_closed(self):
        with self.assertRaises(ValueError):spine_anchor(Image.new("RGBA",(30,30)))
        with self.assertRaises(ValueError):align_cells([],100,100)
        with self.assertRaises(ValueError):align_cells([Image.new("RGBA",(30,30))],10,10)

    def test_all_three_body_landmarks_remain_fixed(self):
        cells=[]
        for dx,dy,wide in [(0,0,0),(5,8,8),(-4,11,15),(3,-2,-3)]:
            image=Image.new("RGBA",(130,140))
            draw=ImageDraw.Draw(image)
            draw.rectangle((20+dx,35+dy,37+dx,110+dy),fill=(80,40,25,255))
            draw.rectangle((38+dx,26+dy,98+dx+wide,109+dy),fill=(180,150,100,255))
            cells.append(image)
        frames,report,mask=rigid_base_cells(cells,124,134)
        self.assertEqual(len(frames),4)
        self.assertGreater(sum(bool(p) for p in mask),200)
        self.assertGreater(sum(0<p<255 for p in mask),30)
        self.assertGreater(sum(p==255 for p in mask),200)
        for landmark in range(3):
            for axis in range(2):
                values=[f["mapped_landmarks"][landmark][axis] for f in report["frames"]]
                self.assertLess(max(values)-min(values),1e-8)

    def test_registration_matches_whole_triangle_not_just_one_anchor(self):
        source=[(2,3),(7,25),(33,20)]
        target=[(5,-23),(0,0),(20,-2)]
        mapping=affine_between(source,target)
        for point,expected in zip(source,target):
            mapped=map_point(mapping,point)
            self.assertAlmostEqual(mapped[0],expected[0])
            self.assertAlmostEqual(mapped[1],expected[1])

    def test_minified_detail_is_filtered_instead_of_aliased(self):
        image=Image.new("RGBA",(128,128))
        for y in range(128):
            for x in range(128):
                shade=255 if (x+y)%2 else 0
                image.putpixel((x,y),(shade,shade,shade,255))
        transform=(.27,0,0,0,.27,0)
        rough=resample_registered(image,transform,(32,32),1)
        filtered=resample_registered(image,transform,(32,32),4)
        def samples(frame):return [frame.getpixel((x,y))[0] for y in range(4,28) for x in range(4,28)]
        self.assertGreater(statistics.pstdev(samples(rough)),40)
        self.assertLess(statistics.pstdev(samples(filtered)),5)
        self.assertAlmostEqual(statistics.mean(samples(filtered)),127,delta=3)

    def test_soft_transparent_edges_keep_original_color(self):
        image=Image.new("RGBA",(100,100))
        ImageDraw.Draw(image).polygon([(7,12),(83,4),(91,84),(15,94)],fill=(210,140,70,255))
        result=resample_registered(image,(.33,0,0,0,.33,0),(34,34),4)
        pixels=[result.getpixel((x,y)) for y in range(result.height) for x in range(result.width)]
        partial=[p for p in pixels if 48<p[3]<220]
        self.assertGreater(len(partial),20)
        for color in partial:
            for actual,expected in zip(color[:3],(210,140,70)):
                self.assertAlmostEqual(actual,expected,delta=7)


if __name__=="__main__":unittest.main()
