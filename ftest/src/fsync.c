#include "test.h"
#include "../../fsync/src/rsync.h"
#include "../../fsync/src/rstream.h"
#include "../../fsync/src/sync_engine.h"
#include <futils/stream.h>
#include <futils/msgbus.h>
#include <fcommon/limits.h>
#include <string.h>
#include <assert.h>

#include <time.h>

// Global data
static fmsgbus_t *msgbus = 0;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// rsync test
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

typedef struct
{
    fostream_t  stream;
    size_t      size;
    size_t      offset;
    uint8_t     data[64 * 1024];
} fdata_ostream_t;

static fostream_t* frsync_ostream_retain(fostream_t *p) { return p; }
static void        frsync_ostream_release(fostream_t *p) { (void)p; }
static bool        frsync_ostream_seek(fostream_t *p, size_t o) { (void)p; (void)o; return true; }

static size_t frsync_ostream_write(fostream_t *stream, char const *data, size_t size)
{
    fdata_ostream_t *pstream = (fdata_ostream_t *)stream;
    memcpy(pstream->data + pstream->size, data, size);
    pstream->size += size;
    return size;
}

static char const FBASE_DATA[] =
    "Alice was beginning to get very tired of sitting by her sister on the bank, and of having nothing to do: once or twice she had peeped into the book her sister was reading, but it had no pictures or conversations in it, 'and what is the use of a book,' thought Alice 'without pictures or conversations?'\n"
    "So she was considering in her own mind (as well as she could, for the hot day made her feel very sleepy and stupid), whether the pleasure of making a daisy-chain would be worth the trouble of getting up and picking the daisies, when suddenly a White Rabbit with pink eyes ran close by her.\n"
    "There was nothing so very remarkable in that; nor did Alice think it so very much out of the way to hear the Rabbit say to itself, 'Oh dear! Oh dear! I shall be late!' (when she thought it over afterwards, it occurred to her that she ought to have wondered at this, but at the time it all seemed quite natural); but when the Rabbit actually took a watch out of its waistcoat-pocket, and looked at it, and then hurried on, Alice started to her feet, for it flashed across her mind that she had never before seen a rabbit with either a waistcoat-pocket, or a watch to take out of it, and burning with curiosity, she ran across the field after it, and fortunately was just in time to see it pop down a large rabbit-hole under the hedge.\n"
    "In another moment down went Alice after it, never once considering how in the world she was to get out again.\n"
    "The rabbit-hole went straight on like a tunnel for some way, and then dipped suddenly down, so suddenly that Alice had not a moment to think about stopping herself before she found herself falling down a very deep well.\n"
    "Either the well was very deep, or she fell very slowly, for she had plenty of time as she went down to look about her and to wonder what was going to happen next. First, she tried to look down and make out what she was coming to, but it was too dark to see anything; then she looked at the sides of the well, and noticed that they were filled with cupboards and book-shelves; here and there she saw maps and pictures hung upon pegs. She took down a jar from one of the shelves as she passed; it was labelled 'ORANGE MARMALADE', but to her great disappointment it was empty: she did not like to drop the jar for fear of killing somebody, so managed to put it into one of the cupboards as she fell past it.\n"
//        "'Well!' thought Alice to herself, 'after such a fall as this, I shall think nothing of tumbling down stairs! How brave they'll all think me at home! Why, I wouldn't say anything about it, even if I fell off the top of the house!' (Which was very likely true.)\n"
//        "Down, down, down. Would the fall never come to an end! 'I wonder how many miles I've fallen by this time?' she said aloud. 'I must be getting somewhere near the centre of the earth. Let me see: that would be four thousand miles down, I think-' (for, you see, Alice had learnt several things of this sort in her lessons in the schoolroom, and though this was not a very good opportunity for showing off her knowledge, as there was no one to listen to her, still it was good practice to say it over) '-yes, that's about the right distance-but then I wonder what Latitude or Longitude I've got to?' (Alice had no idea what Latitude was, or Longitude either, but thought they were nice grand words to say.)\n"
    "Presently she began again. 'I wonder if I shall fall right through the earth! How funny it'll seem to come out among the people that walk with their heads downward! The Antipathies, I think-' (she was rather glad there was no one listening, this time, as it didn't sound at all the right word) '-but I shall have to ask them what the name of the country is, you know. Please, Ma'am, is this New Zealand or Australia?' (and she tried to curtsey as she spoke-fancy curtseying as you're falling through the air! Do you think you could manage it?) 'And what an ignorant little girl she'll think me for asking! No, it'll never do to ask: perhaps I shall see it written up somewhere.'\n"
    "Down, down, down. There was nothing else to do, so Alice soon began talking again. 'Dinah'll miss me very much to-night, I should think!' (Dinah was the cat.) 'I hope they'll remember her saucer of milk at tea-time. Dinah my dear! I wish you were down here with me! There are no mice in the air, I'm afraid, but you might catch a bat, and that's very like a mouse, you know. But do cats eat bats, I wonder?' And here Alice began to get rather sleepy, and went on saying to herself, in a dreamy sort of way, 'Do cats eat bats? Do cats eat bats?' and sometimes, 'Do bats eat cats?' for, you see, as she couldn't answer either question, it didn't much matter which way she put it. She felt that she was dozing off, and had just begun to dream that she was walking hand in hand with Dinah, and saying to her very earnestly, 'Now, Dinah, tell me the truth: did you ever eat a bat?' when suddenly, thump! thump! down she came upon a heap of sticks and dry leaves, and the fall was over.\n"
    "Alice was not a bit hurt, and she jumped up on to her feet in a moment: she looked up, but it was all dark overhead; before her was another long passage, and the White Rabbit was still in sight, hurrying down it. There was not a moment to be lost: away went Alice like the wind, and was just in time to hear it say, as it turned a corner, 'Oh my ears and whiskers, how late it's getting!' She was close behind it when she turned the corner, but the Rabbit was no longer to be seen: she found herself in a long, low hall, which was lit up by a row of lamps hanging from the roof.\n"
    "There were doors all round the hall, but they were all locked; and when Alice had been all the way down one side and up the other, trying every door, she walked sadly down the middle, wondering how she was ever to get out again.\n"
    "Suddenly she came upon a little three-legged table, all made of solid glass; there was nothing on it except a tiny golden key, and Alice's first thought was that it might belong to one of the doors of the hall; but, alas! either the locks were too large, or the key was too small, but at any rate it would not open any of them. However, on the second time round, she came upon a low curtain she had not noticed before, and behind it was a little door about fifteen inches high: she tried the little golden key in the lock, and to her great delight it fitted!\n"
    "Alice opened the door and found that it led into a small passage, not much larger than a rat-hole: she knelt down and looked along the passage into the loveliest garden you ever saw. How she longed to get out of that dark hall, and wander about among those beds of bright flowers and those cool fountains, but she could not even get her head through the doorway; 'and even if my head would go through,' thought poor Alice, 'it would be of very little use without my shoulders. Oh, how I wish I could shut up like a telescope! I think I could, if I only knew how to begin.' For, you see, so many out-of-the-way things had happened lately, that Alice had begun to think that very few things indeed were really impossible.\n"
    "There seemed to be no use in waiting by the little door, so she went back to the table, half hoping she might find another key on it, or at any rate a book of rules for shutting people up like telescopes: this time she found a little bottle on it, ('which certainly was not here before,' said Alice,) and round the neck of the bottle was a paper label, with the words 'DRINK ME' beautifully printed on it in large letters.\n"
    "It was all very well to say 'Drink me,' but the wise little Alice was not going to do that in a hurry. 'No, I'll look first,' she said, 'and see whether it's marked “poison” or not'; for she had read several nice little histories about children who had got burnt, and eaten up by wild beasts and other unpleasant things, all because they would not remember the simple rules their friends had taught them: such as, that a red-hot poker will burn you if you hold it too long; and that if you cut your finger very deeply with a knife, it usually bleeds; and she had never forgotten that, if you drink much from a bottle marked 'poison,' it is almost certain to disagree with you, sooner or later.\n"
    "However, this bottle was not marked 'poison,' so Alice ventured to taste it, and finding it very nice, (it had, in fact, a sort of mixed flavour of cherry-tart, custard, pine-apple, roast turkey, toffee, and hot buttered toast,) she very soon finished it off.\n"
//        "'What a curious feeling!' said Alice; 'I must be shutting up like a telescope.'\n"
//        "And so it was indeed: she was now only ten inches high, and her face brightened up at the thought that she was now the right size for going through the little door into that lovely garden. First, however, she waited for a few minutes to see if she was going to shrink any further: she felt a little nervous about this; 'for it might end, you know,' said Alice to herself, 'in my going out altogether, like a candle. I wonder what I should be like then?' And she tried to fancy what the flame of a candle is like after the candle is blown out, for she could not remember ever having seen such a thing.\n"
    "After a while, finding that nothing more happened, she decided on going into the garden at once; but, alas for poor Alice! when she got to the door, she found she had forgotten the little golden key, and when she went back to the table for it, she found she could not possibly reach it: she could see it quite plainly through the glass, and she tried her best to climb up one of the legs of the table, but it was too slippery; and when she had tired herself out with trying, the poor little thing sat down and cried.\n"
    "'Come, there's no use in crying like that!' said Alice to herself, rather sharply; 'I advise you to leave off this minute!' She generally gave herself very good advice, (though she very seldom followed it), and sometimes she scolded herself so severely as to bring tears into her eyes; and once she remembered trying to box her own ears for having cheated herself in a game of croquet she was playing against herself, for this curious child was very fond of pretending to be two people. 'But it's no use now,' thought poor Alice, 'to pretend to be two people! Why, there's hardly enough of me left to make one respectable person!'\n"
    "Soon her eye fell on a little glass box that was lying under the table: she opened it, and found in it a very small cake, on which the words 'EAT ME' were beautifully marked in currants. 'Well, I'll eat it,' said Alice, 'and if it makes me grow larger, I can reach the key; and if it makes me grow smaller, I can creep under the door; so either way I'll get into the garden, and I don't care which happens!'\n"
    "She ate a little bit, and said anxiously to herself, 'Which way? Which way?', holding her hand on the top of her head to feel which way it was growing, and she was quite surprised to find that she remained the same size: to be sure, this generally happens when one eats cake, but Alice had got so much into the way of expecting nothing but out-of-the-way things to happen, that it seemed quite dull and stupid for life to go on in the common way.\n"
    "So she set to work, and very soon finished off the cake.\n";

static char const FDATA[] =
    "Alice was beginning to get very tired of sitting by her sister on the bank, and of having nothing to do: once or twice she had peeped into the book her sister was reading, but it had no pictures or conversations in it, 'and what is the use of a book,' thought Alice 'without pictures or conversations?'\n"
    "So she was considering in her own mind (as well as she could, for the hot day made her feel very sleepy and stupid), whether the pleasure of making a daisy-chain would be worth the trouble of getting up and picking the daisies, when suddenly a White Rabbit with pink eyes ran close by her.\n"
    "There was nothing so very remarkable in that; nor did Alice think it so very much out of the way to hear the Rabbit say to itself, 'Oh dear! Oh dear! I shall be late!' (when she thought it over afterwards, it occurred to her that she ought to have wondered at this, but at the time it all seemed quite natural); but when the Rabbit actually took a watch out of its waistcoat-pocket, and looked at it, and then hurried on, Alice started to her feet, for it flashed across her mind that she had never before seen a rabbit with either a waistcoat-pocket, or a watch to take out of it, and burning with curiosity, she ran across the field after it, and fortunately was just in time to see it pop down a large rabbit-hole under the hedge.\n"
    "In another moment down went Alice after it, never once considering how in the world she was to get out again.\n"
    "The rabbit-hole went straight on like a tunnel for some way, and then dipped suddenly down, so suddenly that Alice had not a moment to think about stopping herself before she found herself falling down a very deep well.\n"
    "Either the well was very deep, or she fell very slowly, for she had plenty of time as she went down to look about her and to wonder what was going to happen next. First, she tried to look down and make out what she was coming to, but it was too dark to see anything; then she looked at the sides of the well, and noticed that they were filled with cupboards and book-shelves; here and there she saw maps and pictures hung upon pegs. She took down a jar from one of the shelves as she passed; it was labelled 'ORANGE MARMALADE', but to her great disappointment it was empty: she did not like to drop the jar for fear of killing somebody, so managed to put it into one of the cupboards as she fell past it.\n"
    "'Well!' thought Alice to herself, 'after such a fall as this, I shall think nothing of tumbling down stairs! How brave they'll all think me at home! Why, I wouldn't say anything about it, even if I fell off the top of the house!' (Which was very likely true.)\n"
    "Down, down, down. Would the fall never come to an end! 'I wonder how many miles I've fallen by this time?' she said aloud. 'I must be getting somewhere near the centre of the earth. Let me see: that would be four thousand miles down, I think-' (for, you see, Alice had learnt several things of this sort in her lessons in the schoolroom, and though this was not a very good opportunity for showing off her knowledge, as there was no one to listen to her, still it was good practice to say it over) '-yes, that's about the right distance-but then I wonder what Latitude or Longitude I've got to?' (Alice had no idea what Latitude was, or Longitude either, but thought they were nice grand words to say.)\n"
    "Presently she began again. 'I wonder if I shall fall right through the earth! How funny it'll seem to come out among the people that walk with their heads downward! The Antipathies, I think-' (she was rather glad there was no one listening, this time, as it didn't sound at all the right word) '-but I shall have to ask them what the name of the country is, you know. Please, Ma'am, is this New Zealand or Australia?' (and she tried to curtsey as she spoke-fancy curtseying as you're falling through the air! Do you think you could manage it?) 'And what an ignorant little girl she'll think me for asking! No, it'll never do to ask: perhaps I shall see it written up somewhere.'\n"
    "Down, down, down. There was nothing else to do, so Alice soon began talking again. 'Dinah'll miss me very much to-night, I should think!' (Dinah was the cat.) 'I hope they'll remember her saucer of milk at tea-time. Dinah my dear! I wish you were down here with me! There are no mice in the air, I'm afraid, but you might catch a bat, and that's very like a mouse, you know. But do cats eat bats, I wonder?' And here Alice began to get rather sleepy, and went on saying to herself, in a dreamy sort of way, 'Do cats eat bats? Do cats eat bats?' and sometimes, 'Do bats eat cats?' for, you see, as she couldn't answer either question, it didn't much matter which way she put it. She felt that she was dozing off, and had just begun to dream that she was walking hand in hand with Dinah, and saying to her very earnestly, 'Now, Dinah, tell me the truth: did you ever eat a bat?' when suddenly, thump! thump! down she came upon a heap of sticks and dry leaves, and the fall was over.\n"
    "Alice was not a bit hurt, and she jumped up on to her feet in a moment: she looked up, but it was all dark overhead; before her was another long passage, and the White Rabbit was still in sight, hurrying down it. There was not a moment to be lost: away went Alice like the wind, and was just in time to hear it say, as it turned a corner, 'Oh my ears and whiskers, how late it's getting!' She was close behind it when she turned the corner, but the Rabbit was no longer to be seen: she found herself in a long, low hall, which was lit up by a row of lamps hanging from the roof.\n"
    "There were doors all round the hall, but they were all locked; and when Alice had been all the way down one side and up the other, trying every door, she walked sadly down the middle, wondering how she was ever to get out again.\n"
    "Suddenly she came upon a little three-legged table, all made of solid glass; there was nothing on it except a tiny golden key, and Alice's first thought was that it might belong to one of the doors of the hall; but, alas! either the locks were too large, or the key was too small, but at any rate it would not open any of them. However, on the second time round, she came upon a low curtain she had not noticed before, and behind it was a little door about fifteen inches high: she tried the little golden key in the lock, and to her great delight it fitted!\n"
    "Alice opened the door and found that it led into a small passage, not much larger than a rat-hole: she knelt down and looked along the passage into the loveliest garden you ever saw. How she longed to get out of that dark hall, and wander about among those beds of bright flowers and those cool fountains, but she could not even get her head through the doorway; 'and even if my head would go through,' thought poor Alice, 'it would be of very little use without my shoulders. Oh, how I wish I could shut up like a telescope! I think I could, if I only knew how to begin.' For, you see, so many out-of-the-way things had happened lately, that Alice had begun to think that very few things indeed were really impossible.\n"
    "There seemed to be no use in waiting by the little door, so she went back to the table, half hoping she might find another key on it, or at any rate a book of rules for shutting people up like telescopes: this time she found a little bottle on it, ('which certainly was not here before,' said Alice,) and round the neck of the bottle was a paper label, with the words 'DRINK ME' beautifully printed on it in large letters.\n"
    "It was all very well to say 'Drink me,' but the wise little Alice was not going to do that in a hurry. 'No, I'll look first,' she said, 'and see whether it's marked “poison” or not'; for she had read several nice little histories about children who had got burnt, and eaten up by wild beasts and other unpleasant things, all because they would not remember the simple rules their friends had taught them: such as, that a red-hot poker will burn you if you hold it too long; and that if you cut your finger very deeply with a knife, it usually bleeds; and she had never forgotten that, if you drink much from a bottle marked 'poison,' it is almost certain to disagree with you, sooner or later.\n"
    "However, this bottle was not marked 'poison,' so Alice ventured to taste it, and finding it very nice, (it had, in fact, a sort of mixed flavour of cherry-tart, custard, pine-apple, roast turkey, toffee, and hot buttered toast,) she very soon finished it off.\n"
    "'What a curious feeling!' said Alice; 'I must be shutting up like a telescope.'\n"
    "And so it was indeed: she was now only ten inches high, and her face brightened up at the thought that she was now the right size for going through the little door into that lovely garden. First, however, she waited for a few minutes to see if she was going to shrink any further: she felt a little nervous about this; 'for it might end, you know,' said Alice to herself, 'in my going out altogether, like a candle. I wonder what I should be like then?' And she tried to fancy what the flame of a candle is like after the candle is blown out, for she could not remember ever having seen such a thing.\n"
    "After a while, finding that nothing more happened, she decided on going into the garden at once; but, alas for poor Alice! when she got to the door, she found she had forgotten the little golden key, and when she went back to the table for it, she found she could not possibly reach it: she could see it quite plainly through the glass, and she tried her best to climb up one of the legs of the table, but it was too slippery; and when she had tired herself out with trying, the poor little thing sat down and cried.\n"
    "'Come, there's no use in crying like that!' said Alice to herself, rather sharply; 'I advise you to leave off this minute!' She generally gave herself very good advice, (though she very seldom followed it), and sometimes she scolded herself so severely as to bring tears into her eyes; and once she remembered trying to box her own ears for having cheated herself in a game of croquet she was playing against herself, for this curious child was very fond of pretending to be two people. 'But it's no use now,' thought poor Alice, 'to pretend to be two people! Why, there's hardly enough of me left to make one respectable person!'\n"
    "Soon her eye fell on a little glass box that was lying under the table: she opened it, and found in it a very small cake, on which the words 'EAT ME' were beautifully marked in currants. 'Well, I'll eat it,' said Alice, 'and if it makes me grow larger, I can reach the key; and if it makes me grow smaller, I can creep under the door; so either way I'll get into the garden, and I don't care which happens!'\n"
    "She ate a little bit, and said anxiously to herself, 'Which way? Which way?', holding her hand on the top of her head to feel which way it was growing, and she was quite surprised to find that she remained the same size: to be sure, this generally happens when one eats cake, but Alice had got so much into the way of expecting nothing but out-of-the-way things to happen, that it seemed quite dull and stupid for life to go on in the common way.\n"
    "So she set to work, and very soon finished off the cake.\n";

FTEST_START(frsync_algorithm)
{
    ferr_t rc;

    fistream_t *base_data_stream = fmem_const_istream(FBASE_DATA, sizeof FBASE_DATA);
    fistream_t *data_stream = fmem_const_istream(FDATA, sizeof FDATA);

    fdata_ostream_t new_data_ostream =
    {
        {
            frsync_ostream_retain,
            frsync_ostream_release,
            frsync_ostream_write,
            frsync_ostream_seek
        },
        0,
        0
    };

    // Calculate signature
    fdata_ostream_t signature_ostream =
    {
        {
            frsync_ostream_retain,
            frsync_ostream_release,
            frsync_ostream_write,
            frsync_ostream_seek
        },
        0,
        0
    };

    frsync_signature_calculator_t *psig_calc = frsync_signature_calculator_create();
    if (psig_calc)
    {
        rc = frsync_signature_calculate(psig_calc,
                                        base_data_stream,
                                        (fostream_t*)&signature_ostream);               FTEST_ASSERT(rc == FSUCCESS);
        frsync_signature_calculator_release(psig_calc);
    }

    fdata_ostream_t delta_ostream =
    {
        {
            frsync_ostream_retain,
            frsync_ostream_release,
            frsync_ostream_write,
            frsync_ostream_seek
        },
        0,
        0
    };

    // Signature load
    fistream_t *signature_istream = fmem_const_istream((char const *)signature_ostream.data, signature_ostream.size);

    frsync_signature_t *psig = frsync_signature_create();                               FTEST_ASSERT(psig);
    if (psig)
    {
        rc = frsync_signature_load(psig, signature_istream);                            FTEST_ASSERT(rc == FSUCCESS);
        if (rc == FSUCCESS)
        {
            // Delta calculation
            frsync_delta_calculator_t *pdelta_calc = frsync_delta_calculator_create(psig);
            if (pdelta_calc)
            {
                rc = frsync_delta_calculate(pdelta_calc,
                                            data_stream,
                                            (fostream_t*)&delta_ostream);               FTEST_ASSERT(rc == FSUCCESS);
                if (rc == FSUCCESS)
                {
                    fistream_t *delta_istream = fmem_const_istream((char const *)delta_ostream.data, delta_ostream.size);

                    // Delta apply
                    base_data_stream->seek(base_data_stream, 0);
                    frsync_delta_t *pdelta = frsync_delta_create(base_data_stream);
                    if (pdelta)                                                         FTEST_ASSERT(pdelta);
                    {
                        rc = frsync_delta_apply(pdelta,
                                                delta_istream,
                                                (fostream_t *)&new_data_ostream);       FTEST_ASSERT(rc == FSUCCESS);
                        frsync_delta_release(pdelta);
                    }

                    delta_istream->release(delta_istream);
                }
                frsync_delta_calculator_release(pdelta_calc);
            }
        }
        frsync_signature_release(psig);
    }

    signature_istream->release(signature_istream);
    data_stream->release(data_stream);
    base_data_stream->release(base_data_stream);

    FTEST_ASSERT(new_data_ostream.size == sizeof FDATA);
    FTEST_ASSERT(memcmp(new_data_ostream.data, FDATA, sizeof FDATA) == 0);
}
FTEST_END()

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// rstream test
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static fistream_t *pistream = 0;
static fostream_t *postream = 0;

static void fristream_agent(void *ptr, fistream_t *pstream, frstream_info_t const *info)
{
    pistream = pstream->retain(pstream);
}

FTEST_START(frstream)
{
    ferr_t rc;
    (void)rc;

    static fuuid_t const uuid = FUUID(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    frstream_factory_t *rstream_factory = frstream_factory(msgbus, &uuid);
    FTEST_ASSERT(rstream_factory != 0);

    if (rstream_factory)
    {
        rc = frstream_factory_istream_subscribe(rstream_factory, fristream_agent, 0);       FTEST_ASSERT(rc == FSUCCESS);
        postream = frstream_factory_stream(rstream_factory, &uuid, 0);                      FTEST_ASSERT(postream != 0);

        while (!postream || !pistream)
        {
            static struct timespec const F1_SEC = { 1, 0 };
            nanosleep(&F1_SEC, NULL);
        }

        size_t written = postream->write(postream, FDATA, sizeof FDATA);                    FTEST_ASSERT(written == sizeof FDATA);
        (void)written;

        char tmp[sizeof FDATA] = { 0 };
        size_t read_size = pistream->read(pistream, tmp, sizeof tmp);
        FTEST_ASSERT(read_size == sizeof tmp);
        FTEST_ASSERT(memcmp(FDATA, tmp, sizeof FDATA) == 0);
        (void)read_size;

        rc = frstream_factory_istream_unsubscribe(rstream_factory, fristream_agent);        FTEST_ASSERT(rc == FSUCCESS);
        if (pistream) pistream->release(pistream);
        if (postream) postream->release(postream);
        frstream_factory_release(rstream_factory);
    }
}
FTEST_END()

FTEST_START(frstream_fail)
{
    static fuuid_t const uuid = FUUID(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    frstream_factory_t *rstream_factory = frstream_factory(msgbus, &uuid);
    FTEST_ASSERT(rstream_factory != 0);

    if (rstream_factory)
    {
        fostream_t *pstream = frstream_factory_stream(rstream_factory, &uuid, 0);           FTEST_ASSERT(pstream == 0);
        (void)pstream;

        static struct timespec const F1_SEC = { 1, 0 };
        nanosleep(&F1_SEC, NULL);

        frstream_factory_release(rstream_factory);
    }
}
FTEST_END()

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// sync_engine test
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

static fsync_agent_t* fsync_agent_retain(fsync_agent_t *pagent)
{
    return pagent;
}

static void fsync_agent_release(fsync_agent_t *pagent)
{
    (void)pagent;
}

static bool is_sync_completed = false;

static bool fsync_agent_accept(fsync_agent_t *pagent, binn *metainf, fistream_t **pistream, fostream_t **postream)
{
    (void)pagent;
    (void)metainf;

    // dst_istream
    *pistream = fmem_const_istream(FBASE_DATA, sizeof FBASE_DATA);                          assert(*pistream);

    // dst_ostream
    fmem_iostream_t *pstream = fmem_iostream(256);                                          assert(pstream);
    *postream = fmem_ostream(pstream);                                                      assert(*postream);
    fmem_iostream_release(pstream);

    return true;
}

static void fsync_agent_error_handler(fsync_agent_t *pagent, binn *metainf, ferr_t err, char const *err_msg)
{
    is_sync_completed = true;
}

static void fsync_agent_completion_handler(fsync_agent_t *pagent, binn *metainf)
{
    is_sync_completed = true;
}

FTEST_START(fsync_engine)
{
    ferr_t rc;
    (void)rc;

    fistream_t *src_istream = fmem_const_istream(FDATA, sizeof FDATA);                      FTEST_ASSERT(src_istream);

    static fuuid_t const uuid = FUUID(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

    fsync_engine_t *psync_engine = fsync_engine(msgbus, &uuid);                             FTEST_ASSERT(psync_engine);
    if (psync_engine)
    {
        fsync_agent_t agent =
        {
            42,
            fsync_agent_retain,
            fsync_agent_release,
            fsync_agent_accept,
            fsync_agent_error_handler,
            fsync_agent_completion_handler
        };
        rc = fsync_engine_register_agent(psync_engine, &agent);                             FTEST_ASSERT(rc == FSUCCESS);
        rc = fsync_engine_sync(psync_engine, &uuid, 42, 0, src_istream);                    FTEST_ASSERT(rc == FSUCCESS);

        while(!is_sync_completed)
        {
            static struct timespec const F1_SEC = { 1, 0 };
            nanosleep(&F1_SEC, NULL);
        }

        // TODO
        fsync_engine_release(psync_engine);
    }

    src_istream->release(src_istream);
}
FTEST_END()

FUNIT_TEST_START(fsync)
    assert(fmsgbus_create(&msgbus, FMSGBUS_THREADS_NUM) == FSUCCESS);

    FTEST(frsync_algorithm);
    FTEST(frstream);
    FTEST(frstream_fail);
    FTEST(fsync_engine);

    fmsgbus_release(msgbus);

FUNIT_TEST_END()
