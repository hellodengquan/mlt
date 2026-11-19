/*
 * Copyright (C) 2019-2026 Meltytech, LLC
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with consumer library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <QtTest>

#include <mlt++/Mlt.h>
using namespace Mlt;

class TestPlaylist : public QObject
{
    Q_OBJECT
    Profile profile;

public:
    TestPlaylist()
        : profile("dv_pal")
    {
        Factory::init();
    }

private Q_SLOTS:

    void Append()
    {
        Playlist pl(profile);
        QVERIFY(pl.is_valid());
        Producer p(profile, "noise");
        QVERIFY(p.is_valid());
        QCOMPARE(pl.count(), 0);
        int result = pl.append(p);
        QCOMPARE(result, 0); // Success
        QCOMPARE(pl.count(), 1);
    }

    void Remove()
    {
        Playlist pl(profile);
        QVERIFY(pl.is_valid());
        Producer p(profile, "noise");
        QVERIFY(p.is_valid());
        QCOMPARE(pl.count(), 0);
        pl.append(p);
        QCOMPARE(pl.count(), 1);
        int result = pl.remove(0);
        QCOMPARE(result, 0); // Success
        QCOMPARE(pl.count(), 0);
    }

    void RemoveErrorOnInvalidIndex()
    {
        Playlist pl(profile);
        QVERIFY(pl.is_valid());
        Producer p(profile, "noise");
        QVERIFY(p.is_valid());
        QCOMPARE(pl.count(), 0);
        pl.append(p);
        QCOMPARE(pl.count(), 1);
        int result = pl.remove(10); // Invalid index
        QCOMPARE(result, 1);        // Fail
        QCOMPARE(pl.count(), 1);
    }

    void Move()
    {
        Playlist pl(profile);
        QVERIFY(pl.is_valid());

        // Initial order: 1, 2, 3
        Producer p1(profile, "noise");
        p1.set("id", 1);
        QVERIFY(p1.is_valid());
        pl.append(p1);
        Producer p2(profile, "noise");
        p2.set("id", 2);
        QVERIFY(p2.is_valid());
        pl.append(p2);
        Producer p3(profile, "noise");
        p3.set("id", 3);
        QVERIFY(p3.is_valid());
        pl.append(p3);
        QCOMPARE(pl.count(), 3);

        // Move first to last
        int result = pl.move(0, 2);
        QCOMPARE(result, 0); // Success
        QCOMPARE(pl.count(), 3);

        // New order: 2, 3, 1
        Producer *pp1 = pl.get_clip(0);
        Producer *pp2 = pl.get_clip(1);
        Producer *pp3 = pl.get_clip(2);
        QCOMPARE(pp1->parent().get_int("id"), 2);
        QCOMPARE(pp2->parent().get_int("id"), 3);
        QCOMPARE(pp3->parent().get_int("id"), 1);
        delete pp1;
        delete pp2;
        delete pp3;
    }

    void MoveCorrectInvalidIndex()
    {
        Playlist pl(profile);
        QVERIFY(pl.is_valid());

        // Initial order: 1, 2, 3
        Producer p1(profile, "noise");
        p1.set("id", 1);
        QVERIFY(p1.is_valid());
        pl.append(p1);
        Producer p2(profile, "noise");
        p2.set("id", 2);
        QVERIFY(p2.is_valid());
        pl.append(p2);
        Producer p3(profile, "noise");
        p3.set("id", 3);
        QVERIFY(p3.is_valid());
        pl.append(p3);
        QCOMPARE(pl.count(), 3);

        // Move first to an invalid index
        int result = pl.move(0, 10); // 10 is invalid
        // This is still successful because move() corrects the index to be the last entry.
        QCOMPARE(result, 0); // Success
        QCOMPARE(pl.count(), 3);

        // The first will be moved to the last after move() corrects the index.
        Producer *pp1 = pl.get_clip(0);
        Producer *pp2 = pl.get_clip(1);
        Producer *pp3 = pl.get_clip(2);
        QCOMPARE(pp1->parent().get_int("id"), 2);
        QCOMPARE(pp2->parent().get_int("id"), 3);
        QCOMPARE(pp3->parent().get_int("id"), 1);
        delete pp1;
        delete pp2;
        delete pp3;
    }

    void Reorder()
    {
        Playlist pl(profile);
        QVERIFY(pl.is_valid());

        // Initial order: 1, 2, 3
        Producer p1(profile, "noise");
        p1.set("id", 1);
        QVERIFY(p1.is_valid());
        pl.append(p1);
        Producer p2(profile, "noise");
        p2.set("id", 2);
        QVERIFY(p2.is_valid());
        pl.append(p2);
        Producer p3(profile, "noise");
        p3.set("id", 3);
        QVERIFY(p3.is_valid());
        pl.append(p3);
        QCOMPARE(pl.count(), 3);

        // Reverse the order of the clips.
        int new_order[] = {2, 1, 0};
        int result = pl.reorder(new_order);
        QCOMPARE(result, 0); // Success
        QCOMPARE(pl.count(), 3);

        // The order will be reversed.
        Producer *pp1 = pl.get_clip(0);
        Producer *pp2 = pl.get_clip(1);
        Producer *pp3 = pl.get_clip(2);
        QCOMPARE(pp1->parent().get_int("id"), 3);
        QCOMPARE(pp2->parent().get_int("id"), 2);
        QCOMPARE(pp3->parent().get_int("id"), 1);
        delete pp1;
        delete pp2;
        delete pp3;
    }

    void ReorderErrorOnDuplicate()
    {
        Playlist pl(profile);
        QVERIFY(pl.is_valid());

        // Initial order: 1, 2, 3
        Producer p1(profile, "noise");
        p1.set("id", 1);
        QVERIFY(p1.is_valid());
        pl.append(p1);
        Producer p2(profile, "noise");
        p2.set("id", 2);
        QVERIFY(p2.is_valid());
        pl.append(p2);
        Producer p3(profile, "noise");
        p3.set("id", 3);
        QVERIFY(p3.is_valid());
        pl.append(p3);
        QCOMPARE(pl.count(), 3);

        // Provide an order with duplicate indices.
        int new_order[] = {2, 2, 2};
        int result = pl.reorder(new_order);
        QCOMPARE(result, 1); // Fail
        QCOMPARE(pl.count(), 3);

        // The order will unchanged.
        Producer *pp1 = pl.get_clip(0);
        Producer *pp2 = pl.get_clip(1);
        Producer *pp3 = pl.get_clip(2);
        QCOMPARE(pp1->parent().get_int("id"), 1);
        QCOMPARE(pp2->parent().get_int("id"), 2);
        QCOMPARE(pp3->parent().get_int("id"), 3);
        delete pp1;
        delete pp2;
        delete pp3;
    }

    void ReorderErrorOnInvalidIndex()
    {
        Playlist pl(profile);
        QVERIFY(pl.is_valid());

        // Initial order: 1, 2, 3
        Producer p1(profile, "noise");
        p1.set("id", 1);
        QVERIFY(p1.is_valid());
        pl.append(p1);
        Producer p2(profile, "noise");
        p2.set("id", 2);
        QVERIFY(p2.is_valid());
        pl.append(p2);
        Producer p3(profile, "noise");
        p3.set("id", 3);
        QVERIFY(p3.is_valid());
        pl.append(p3);
        QCOMPARE(pl.count(), 3);

        // Provide an order with an invalid index.
        int new_order[] = {0, 1, 10};
        int result = pl.reorder(new_order);
        QCOMPARE(result, 1); // Fail
        QCOMPARE(pl.count(), 3);

        // The order will unchanged.
        Producer *pp1 = pl.get_clip(0);
        Producer *pp2 = pl.get_clip(1);
        Producer *pp3 = pl.get_clip(2);
        QCOMPARE(pp1->parent().get_int("id"), 1);
        QCOMPARE(pp2->parent().get_int("id"), 2);
        QCOMPARE(pp3->parent().get_int("id"), 3);
        delete pp1;
        delete pp2;
        delete pp3;
    }

    void FxCutFrameReceivesProducerMeta()
    {
        Producer p(profile, "color", "0x00000000");
        QVERIFY(p.is_valid());
        p.set("mlt_image_format", "rgba");
        p.set("meta.fx_cut", 1);
        p.set("meta.test.flag", 1);
        p.set("set.consumer.dummy", 1);
        p.set_in_and_out(0, 9);

        Playlist pl(profile);
        pl.append(p);

        Frame *frame = pl.get_frame();
        QVERIFY(frame != NULL);
        QCOMPARE(frame->get_int("fx_cut"), 1);
        QCOMPARE(frame->get_int("meta.test.flag"), 1);
        QCOMPARE(frame->get_int("consumer.dummy"), 1);
        delete frame;
    }

    void RefreshDetectsCutChange()
    {
        Producer p(profile, "color", "red");
        p.set("length", 100);
        p.set_in_and_out(0, 99);
        Playlist pl(profile);
        for (int i = 0; i < 4; i++)
            pl.append(p, 0, 9);
        QCOMPARE(pl.get_playtime(), 40);

        // A cut does not notify the playlist when its points change.
        Producer *cut = pl.get_clip(0);
        QVERIFY(cut != nullptr);
        cut->set_in_and_out(0, 4);
        delete cut;
        QCOMPARE(pl.clip_length(0), 10);

        // The next edit refreshes the playlist from the producers.
        pl.blank(9);
        QCOMPARE(pl.clip_length(0), 5);
        QCOMPARE(pl.clip_start(1), 5);
        QCOMPARE(pl.get_playtime(), 45);

        cut = pl.get_clip(2);
        QVERIFY(cut != nullptr);
        cut->set_in_and_out(2, 4);
        delete cut;
        QCOMPARE(pl.resize_clip(1, 0, 19), 0);
        QCOMPARE(pl.clip_length(1), 20);
        QCOMPARE(pl.clip_length(2), 3);
        QCOMPARE(pl.clip_start(3), 28);
        QCOMPARE(pl.get_playtime(), 48);

        // So does a change of the parent producer.
        cut = pl.get_clip(3);
        QVERIFY(cut != nullptr);
        cut->set_in_and_out(0, 0);
        delete cut;
        p.set("out", 98);
        QCOMPARE(pl.clip_length(3), 1);
        QCOMPARE(pl.get_playtime(), 39);
    }

    void BlankStatus()
    {
        Producer p(profile, "color", "red");
        p.set("length", 100);
        p.set_in_and_out(0, 99);
        Playlist pl(profile);
        pl.blank(4);
        pl.append(p, 0, 9);
        pl.blank(4);
        pl.blank(2);
        pl.append(p, 0, 9);
        pl.blank(6);
        QCOMPARE(pl.count(), 6);
        QVERIFY(pl.is_blank(0));
        QVERIFY(!pl.is_blank(1));
        QVERIFY(pl.is_blank(2));
        QVERIFY(pl.is_blank(3));
        QVERIFY(!pl.is_blank(4));
        QVERIFY(pl.is_blank(5));
        QVERIFY(pl.is_blank(-1));
        QVERIFY(pl.is_blank(6));
        QVERIFY(pl.is_blank_at(0));
        QVERIFY(!pl.is_blank_at(5));
        QVERIFY(pl.is_blank_at(15));
        QVERIFY(pl.is_blank_at(1000));

        pl.consolidate_blanks(0);
        QCOMPARE(pl.count(), 4);
        QVERIFY(pl.is_blank(0));
        QVERIFY(!pl.is_blank(1));
        QVERIFY(pl.is_blank(2));
        QCOMPARE(pl.clip_length(2), 8);
        QVERIFY(!pl.is_blank(3));
        QCOMPARE(pl.get_playtime(), 33);

        // A clip replaced by a blank is merged with its neighbours.
        Producer *clip = pl.replace_with_blank(3);
        QVERIFY(clip != nullptr);
        delete clip;
        QVERIFY(pl.is_blank(3));
        pl.consolidate_blanks(1);
        QCOMPARE(pl.count(), 3);
        QCOMPARE(pl.clip_length(2), 18);
        pl.consolidate_blanks(0);
        QCOMPARE(pl.count(), 2);
        QCOMPARE(pl.get_playtime(), 15);
    }

    void MixDoesNotDoubleCountDuration()
    {
        Producer red(profile, "color", "red");
        QVERIFY(red.is_valid());
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        QVERIFY(blue.is_valid());
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 0, 9);
        QCOMPARE(pl.count(), 2);
        QCOMPARE(pl.get_playtime(), 20);

        // A 4-frame mix takes its frames from both clips; it must not add to the total.
        QCOMPARE(pl.mix(0, 4), 0);
        QCOMPARE(pl.count(), 3);
        QVERIFY(!pl.is_mix(0));
        QVERIFY(pl.is_mix(1));
        QVERIFY(!pl.is_mix(2));
        QCOMPARE(pl.clip_length(0), 6);
        QCOMPARE(pl.clip_length(1), 4);
        QCOMPARE(pl.clip_length(2), 6);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 6);
        QCOMPARE(pl.clip_start(2), 10);
        QCOMPARE(pl.get_playtime(), 16);

        // The overlap takes frames from the tail of clip A and the head of clip B.
        ClipInfo *infoA = pl.clip_info(0);
        ClipInfo *infoB = pl.clip_info(2);
        QVERIFY(infoA != nullptr);
        QVERIFY(infoB != nullptr);
        QCOMPARE(infoA->frame_in, 0);
        QCOMPARE(infoA->frame_out, 5);
        QCOMPARE(infoB->frame_in, 4);
        QCOMPARE(infoB->frame_out, 9);
        delete infoA;
        delete infoB;

        // Every output frame reports a source position consistent with the
        // clip layout, and the overlap is counted only once in the playtime.
        const int starts[] = {0, 6, 10};
        const int lengths[] = {6, 4, 6};
        const int clips[] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2};
        for (int pos = 0; pos < pl.get_playtime(); pos++) {
            pl.seek(pos);
            Frame *frame = pl.get_frame();
            QVERIFY(frame != nullptr);
            QCOMPARE(frame->get_position(), pos);
            int clip = clips[pos];
            QCOMPARE(frame->get_int("meta.playlist.clip_position"), pos - starts[clip]);
            QCOMPARE(frame->get_int("meta.playlist.clip_length"), lengths[clip]);
            delete frame;
        }

        // The frames before and after the overlap come from the correct source clips.
        mlt_image_format format = mlt_image_rgba;
        int w = 0, h = 0;
        pl.seek(0);
        Frame *first = pl.get_frame();
        QVERIFY(first != nullptr);
        uint8_t *image = first->get_image(format, w, h);
        QVERIFY(image != nullptr);
        QVERIFY(image[0] > 200); // red channel
        QVERIFY(image[2] < 60);  // blue channel
        delete first;

        pl.seek(15);
        Frame *last = pl.get_frame();
        QVERIFY(last != nullptr);
        image = last->get_image(format, w, h);
        QVERIFY(image != nullptr);
        QVERIFY(image[2] > 200); // blue channel
        QVERIFY(image[0] < 60);  // red channel
        delete last;
    }

    void ResizeMixReadjustsNeighbours()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 0, 9);
        QCOMPARE(pl.mix(0, 4), 0);
        QCOMPARE(pl.get_playtime(), 16);

        // Growing the mix takes frames back from both neighbours.
        QCOMPARE(pl.resize_clip(1, 0, 5), 0);
        QCOMPARE(pl.clip_length(0), 4);
        QCOMPARE(pl.clip_length(1), 6);
        QCOMPARE(pl.clip_length(2), 4);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 4);
        QCOMPARE(pl.clip_start(2), 10);
        QCOMPARE(pl.get_playtime(), 14);

        // Shrinking the mix returns frames to both neighbours.
        QCOMPARE(pl.resize_clip(1, 0, 1), 0);
        QCOMPARE(pl.clip_length(0), 8);
        QCOMPARE(pl.clip_length(1), 2);
        QCOMPARE(pl.clip_length(2), 8);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 8);
        QCOMPARE(pl.clip_start(2), 10);
        QCOMPARE(pl.get_playtime(), 18);

        // The neighbours' source in/out points track the mix length.
        ClipInfo *infoA = pl.clip_info(0);
        ClipInfo *infoB = pl.clip_info(2);
        QVERIFY(infoA != nullptr);
        QVERIFY(infoB != nullptr);
        QCOMPARE(infoA->frame_in, 0);
        QCOMPARE(infoA->frame_out, 7);
        QCOMPARE(infoB->frame_in, 2);
        QCOMPARE(infoB->frame_out, 9);
        delete infoA;
        delete infoB;
    }

    void TrimClipShiftsMixAndBlank()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 0, 9);
        pl.blank(4); // 5 frames
        QCOMPARE(pl.mix(0, 4), 0);
        QCOMPARE(pl.count(), 4);
        QCOMPARE(pl.get_playtime(), 21);

        // Trimming clip A shorter shifts the mix and everything after it.
        QCOMPARE(pl.resize_clip(0, 0, 2), 0);
        QCOMPARE(pl.clip_length(0), 3);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 3);
        QCOMPARE(pl.clip_start(2), 7);
        QCOMPARE(pl.clip_start(3), 13);
        QVERIFY(pl.is_mix(1));
        QVERIFY(pl.is_blank(3));
        QCOMPARE(pl.get_playtime(), 18);

        // The blank adjacent to the mixed clips can be resized like any clip.
        QCOMPARE(pl.resize_clip(3, 0, 9), 0);
        QCOMPARE(pl.clip_length(3), 10);
        QCOMPARE(pl.get_playtime(), 23);

        // A second blank appended after it consolidates into one blank.
        pl.blank(2); // 3 frames
        QCOMPARE(pl.count(), 5);
        pl.consolidate_blanks(1);
        QCOMPARE(pl.count(), 4);
        QVERIFY(pl.is_blank(3));
        QCOMPARE(pl.clip_length(3), 13);
        QCOMPARE(pl.get_playtime(), 26);
    }

    void MixInKeepsPlaytime()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 0, 9);
        QCOMPARE(pl.get_playtime(), 20);

        // mix_in keeps clip A intact and takes the mix frames from beyond
        // clip A's out point; clip B gives up its head. Total is unchanged.
        QCOMPARE(pl.mix_in(0, 4), 0);
        QCOMPARE(pl.count(), 3);
        QVERIFY(pl.is_mix(1));
        QCOMPARE(pl.clip_length(0), 10);
        QCOMPARE(pl.clip_length(1), 4);
        QCOMPARE(pl.clip_length(2), 6);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 10);
        QCOMPARE(pl.clip_start(2), 14);
        QCOMPARE(pl.get_playtime(), 20);

        ClipInfo *infoA = pl.clip_info(0);
        ClipInfo *infoB = pl.clip_info(2);
        QVERIFY(infoA != nullptr);
        QVERIFY(infoB != nullptr);
        QCOMPARE(infoA->frame_in, 0);
        QCOMPARE(infoA->frame_out, 9);
        QCOMPARE(infoB->frame_in, 4);
        QCOMPARE(infoB->frame_out, 9);
        delete infoA;
        delete infoB;
    }

    void MixOutKeepsPlaytime()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 6, 15);
        QCOMPARE(pl.get_playtime(), 20);

        // mix_out keeps clip B intact and takes the mix frames from before
        // clip B's in point; clip A gives up its tail. Total is unchanged.
        QCOMPARE(pl.mix_out(0, 4), 0);
        QCOMPARE(pl.count(), 3);
        QVERIFY(pl.is_mix(1));
        QCOMPARE(pl.clip_length(0), 6);
        QCOMPARE(pl.clip_length(1), 4);
        QCOMPARE(pl.clip_length(2), 10);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 6);
        QCOMPARE(pl.clip_start(2), 10);
        QCOMPARE(pl.get_playtime(), 20);

        ClipInfo *infoA = pl.clip_info(0);
        ClipInfo *infoB = pl.clip_info(2);
        QVERIFY(infoA != nullptr);
        QVERIFY(infoB != nullptr);
        QCOMPARE(infoA->frame_in, 0);
        QCOMPARE(infoA->frame_out, 5);
        QCOMPARE(infoB->frame_in, 6);
        QCOMPARE(infoB->frame_out, 15);
        delete infoA;
        delete infoB;
    }

    void MixOutAbsorbsClipB()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 4, 13);
        QCOMPARE(pl.get_playtime(), 20);

        // When the mix length reaches all the way back to the start of clip
        // B's source, clip B is absorbed into the mix: the mix entry spans
        // the overlap plus the remainder of clip B, and the frames before
        // clip B's in point are not counted twice.
        QCOMPARE(pl.mix_out(0, 4), 0);
        QCOMPARE(pl.count(), 2);
        QVERIFY(!pl.is_mix(0));
        QVERIFY(pl.is_mix(1));
        QCOMPARE(pl.clip_length(0), 6);
        QCOMPARE(pl.clip_length(1), 10);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 6);
        QCOMPARE(pl.get_playtime(), 16);
    }

    void RemoveMixRestoresClips()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 0, 9);
        QCOMPARE(pl.mix(0, 4), 0);
        QCOMPARE(pl.get_playtime(), 16);

        // Removing the mix returns the borrowed frames to both clips.
        QCOMPARE(pl.remove(1), 0);
        QCOMPARE(pl.count(), 2);
        QVERIFY(!pl.is_mix(0));
        QVERIFY(!pl.is_mix(1));
        QCOMPARE(pl.clip_length(0), 10);
        QCOMPARE(pl.clip_length(1), 10);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 10);
        QCOMPARE(pl.get_playtime(), 20);

        ClipInfo *infoA = pl.clip_info(0);
        ClipInfo *infoB = pl.clip_info(1);
        QVERIFY(infoA != nullptr);
        QVERIFY(infoB != nullptr);
        QCOMPARE(infoA->frame_in, 0);
        QCOMPARE(infoA->frame_out, 9);
        QCOMPARE(infoB->frame_in, 0);
        QCOMPARE(infoB->frame_out, 9);
        delete infoA;
        delete infoB;
    }

    void MixConsumesEntireClips()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 0, 9);

        // A mix longer than either clip is clamped and consumes both clips.
        QCOMPARE(pl.mix(0, 50), 0);
        QCOMPARE(pl.count(), 1);
        QVERIFY(pl.is_mix(0));
        QCOMPARE(pl.clip_length(0), 10);
        QCOMPARE(pl.get_playtime(), 10);

        // Removing the mix restores both clips in full.
        QCOMPARE(pl.remove(0), 0);
        QCOMPARE(pl.count(), 2);
        QCOMPARE(pl.clip_length(0), 10);
        QCOMPARE(pl.clip_length(1), 10);
        QCOMPARE(pl.get_playtime(), 20);
    }

    void MixTransitionAttachment()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 0, 9);

        // A transition passed to mix() is trimmed to the mix length and does
        // not change the timing of the playlist.
        Transition luma(profile, "luma");
        QVERIFY(luma.is_valid());
        QCOMPARE(pl.mix(0, 4, &luma), 0);
        QCOMPARE(luma.get_in(), 0);
        QCOMPARE(luma.get_out(), 3);
        QCOMPARE(pl.get_playtime(), 16);
        QCOMPARE(pl.clip_length(1), 4);

        // A transition can also be attached to an existing mix.
        Transition audioMix(profile, "mix");
        QVERIFY(audioMix.is_valid());
        QCOMPARE(pl.mix_add(1, &audioMix), 0);
        QCOMPARE(audioMix.get_in(), 0);
        QCOMPARE(audioMix.get_out(), 3);
        QCOMPARE(pl.get_playtime(), 16);

        // Attaching to a clip that is not a mix fails without side effects.
        Transition other(profile, "luma");
        QVERIFY(other.is_valid());
        QCOMPARE(pl.mix_add(0, &other), 1);
        QCOMPARE(pl.mix_add(2, &other), 1);
        QCOMPARE(pl.mix_add(10, &other), 1);
        QCOMPARE(pl.get_playtime(), 16);
        QCOMPARE(pl.count(), 3);
    }

    void InvalidOperationsHaveClearResults()
    {
        Producer red(profile, "color", "red");
        red.set("length", 100);
        red.set_in_and_out(0, 99);
        Producer blue(profile, "color", "blue");
        blue.set("length", 100);
        blue.set_in_and_out(0, 99);

        Playlist pl(profile);
        pl.append(red, 0, 9);
        pl.append(blue, 0, 9);
        QCOMPARE(pl.mix(0, 4), 0);
        QCOMPARE(pl.count(), 3);
        QCOMPARE(pl.get_playtime(), 16);

        // Mixes cannot start on or after the last clip, or at invalid indices.
        QCOMPARE(pl.mix(-1, 4), 1);
        QCOMPARE(pl.mix(2, 4), 1);
        QCOMPARE(pl.mix(10, 4), 1);
        QCOMPARE(pl.mix_in(-1, 4), 1);
        QCOMPARE(pl.mix_in(2, 4), 1);
        QCOMPARE(pl.mix_out(-1, 4), 1);
        QCOMPARE(pl.mix_out(2, 4), 1);

        // Resizing an invalid clip index fails.
        QCOMPARE(pl.resize_clip(-1, 0, 5), 1);
        QCOMPARE(pl.resize_clip(3, 0, 5), 1);

        // Invalid indices yield well-defined sentinel results that no valid
        // clip can produce: a null handle, zero length, and a start at the
        // end of the playlist.
        QVERIFY(pl.get_clip(-1) == nullptr);
        QVERIFY(pl.get_clip(3) == nullptr);
        QVERIFY(pl.get_clip_at(1000) == nullptr);
        QCOMPARE(pl.clip_length(-1), 0);
        QCOMPARE(pl.clip_length(3), 0);
        QCOMPARE(pl.clip_start(-1), 0);
        QCOMPARE(pl.clip_start(3), pl.get_playtime());
        QVERIFY(!pl.is_mix(-1));
        QVERIFY(!pl.is_mix(3));
        QVERIFY(pl.is_blank(-1));
        QVERIFY(pl.is_blank(3));

        // None of the failed operations changed the output timing.
        QCOMPARE(pl.count(), 3);
        QCOMPARE(pl.clip_length(0), 6);
        QCOMPARE(pl.clip_length(1), 4);
        QCOMPARE(pl.clip_length(2), 6);
        QCOMPARE(pl.clip_start(0), 0);
        QCOMPARE(pl.clip_start(1), 6);
        QCOMPARE(pl.clip_start(2), 10);
        QCOMPARE(pl.get_playtime(), 16);
    }
};

QTEST_APPLESS_MAIN(TestPlaylist)

#include "test_playlist.moc"
