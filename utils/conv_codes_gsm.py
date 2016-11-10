#!/usr/bin/python2

from conv_gen import ConvolutionalCode
from conv_gen import poly

# Polynomials according to 3GPP TS 05.03 Annex B
G0 = poly(0, 3, 4)
G1 = poly(0, 1, 3, 4)
G2 = poly(0, 2, 4)
G3 = poly(0, 1, 2, 3, 4)
G4 = poly(0, 2, 3, 5, 6)
G5 = poly(0, 1, 4, 6)
G6 = poly(0, 1, 2, 3, 4, 6)
G7 = poly(0, 1, 2, 3, 6)

# Shared polynomials
shared_polys = {
	"xcch" : [
		( G0, 1 ),
		( G1, 1 ),
	],
	"mcs" : [
		( G4, 1 ),
		( G7, 1 ),
		( G5, 1 ),
	],
}

# Convolutional code definitions
conv_codes = [
	# xCCH definition
	ConvolutionalCode(
		224,
		shared_polys["xcch"],
		name = "xcch",
		description = [
			"xCCH convolutional code:",
			"228 bits blocks, rate 1/2, k = 5",
			"G0 = 1 + D3 + D4",
			"G1 = 1 + D + D3 + D4",
		]
	),

	# RACH definition
	ConvolutionalCode(
		14,
		shared_polys["xcch"],
		name = "rach",
		description = ["RACH convolutional code"]
	),

	# SCH definition
	ConvolutionalCode(
		35,
		shared_polys["xcch"],
		name = "sch",
		description = ["SCH convolutional code"]
	),

	# CS2 definition
	ConvolutionalCode(
		290,
		shared_polys["xcch"],
		puncture = [
			 15,  19,  23,  27,  31,  35,  43,  47,  51,  55,  59,  63,  67,  71,
			 75,  79,  83,  91,  95,  99, 103, 107, 111, 115, 119, 123, 127, 131,
			139, 143, 147, 151, 155, 159, 163, 167, 171, 175, 179, 187, 191, 195,
			199, 203, 207, 211, 215, 219, 223, 227, 235, 239, 243, 247, 251, 255,
			259, 263, 267, 271, 275, 283, 287, 291, 295, 299, 303, 307, 311, 315,
			319, 323, 331, 335, 339, 343, 347, 351, 355, 359, 363, 367, 371, 379,
			383, 387, 391, 395, 399, 403, 407, 411, 415, 419, 427, 431, 435, 439,
			443, 447, 451, 455, 459, 463, 467, 475, 479, 483, 487, 491, 495, 499,
			503, 507, 511, 515, 523, 527, 531, 535, 539, 543, 547, 551, 555, 559,
			563, 571, 575, 579, 583, 587, -1
		],
		name = "cs2",
		description = [
			"CS2 convolutional code:",
			"G0 = 1 + D3 + D4",
			"G1 = 1 + D + D3 + D4",
		]
	),

	# CS3 definition
	ConvolutionalCode(
		334,
		shared_polys["xcch"],
		puncture = [
			 15,  17,  21,  23,  27,  29,  33,  35,  39,  41,  45,  47,  51,  53,
			 57,  59,  63,  65,  69,  71,  75,  77,  81,  83,  87,  89,  93,  95,
			 99, 101, 105, 107, 111, 113, 117, 119, 123, 125, 129, 131, 135, 137,
			141, 143, 147, 149, 153, 155, 159, 161, 165, 167, 171, 173, 177, 179,
			183, 185, 189, 191, 195, 197, 201, 203, 207, 209, 213, 215, 219, 221,
			225, 227, 231, 233, 237, 239, 243, 245, 249, 251, 255, 257, 261, 263,
			267, 269, 273, 275, 279, 281, 285, 287, 291, 293, 297, 299, 303, 305,
			309, 311, 315, 317, 321, 323, 327, 329, 333, 335, 339, 341, 345, 347,
			351, 353, 357, 359, 363, 365, 369, 371, 375, 377, 381, 383, 387, 389,
			393, 395, 399, 401, 405, 407, 411, 413, 417, 419, 423, 425, 429, 431,
			435, 437, 441, 443, 447, 449, 453, 455, 459, 461, 465, 467, 471, 473,
			477, 479, 483, 485, 489, 491, 495, 497, 501, 503, 507, 509, 513, 515,
			519, 521, 525, 527, 531, 533, 537, 539, 543, 545, 549, 551, 555, 557,
			561, 563, 567, 569, 573, 575, 579, 581, 585, 587, 591, 593, 597, 599,
			603, 605, 609, 611, 615, 617, 621, 623, 627, 629, 633, 635, 639, 641,
			645, 647, 651, 653, 657, 659, 663, 665, 669, 671, -1
		],
		name = "cs3",
		description = [
			"CS3 convolutional code:",
			"G0 = 1 + D3 + D4",
			"G1 = 1 + D + D3 + D4",
		]
	),

	# TCH_AFS_12_2 definition
	ConvolutionalCode(
		250,
		[
			(  1,  1 ),
			( G1, G0 ),
		],
		puncture = [
			321, 325, 329, 333, 337, 341, 345, 349, 353, 357, 361, 363,
			365, 369, 373, 377, 379, 381, 385, 389, 393, 395, 397, 401,
			405, 409, 411, 413, 417, 421, 425, 427, 429, 433, 437, 441,
			443, 445, 449, 453, 457, 459, 461, 465, 469, 473, 475, 477,
			481, 485, 489, 491, 493, 495, 497, 499, 501, 503, 505, 507,
			-1
		],
		name = 'tch_afs_12_2',
		description = [
			"TCH/AFS 12.2 kbits convolutional code:",
			"250 bits block, rate 1/2, punctured",
			"G0/G0 = 1",
			"G1/G0 = 1 + D + D3 + D4 / 1 + D3 + D4",
		]
	),

	# TCH_AFS_10_2 definition
	ConvolutionalCode(
		210,
		[
			( G1, G3 ),
			( G2, G3 ),
			(  1,  1 ),
		],
		puncture = [
			  1,   4,   7,  10,  16,  19,  22,  28,  31,  34,  40,  43,
			 46,  52,  55,  58,  64,  67,  70,  76,  79,  82,  88,  91,
			 94, 100, 103, 106, 112, 115, 118, 124, 127, 130, 136, 139,
			142, 148, 151, 154, 160, 163, 166, 172, 175, 178, 184, 187,
			190, 196, 199, 202, 208, 211, 214, 220, 223, 226, 232, 235,
			238, 244, 247, 250, 256, 259, 262, 268, 271, 274, 280, 283,
			286, 292, 295, 298, 304, 307, 310, 316, 319, 322, 325, 328,
			331, 334, 337, 340, 343, 346, 349, 352, 355, 358, 361, 364,
			367, 370, 373, 376, 379, 382, 385, 388, 391, 394, 397, 400,
			403, 406, 409, 412, 415, 418, 421, 424, 427, 430, 433, 436,
			439, 442, 445, 448, 451, 454, 457, 460, 463, 466, 469, 472,
			475, 478, 481, 484, 487, 490, 493, 496, 499, 502, 505, 508,
			511, 514, 517, 520, 523, 526, 529, 532, 535, 538, 541, 544,
			547, 550, 553, 556, 559, 562, 565, 568, 571, 574, 577, 580,
			583, 586, 589, 592, 595, 598, 601, 604, 607, 609, 610, 613,
			616, 619, 621, 622, 625, 627, 628, 631, 633, 634, 636, 637,
			639, 640, -1
		],
		name = 'tch_afs_10_2',
		description = [
			"TCH/AFS 10.2 kbits convolutional code:",
			"G1/G3 = 1 + D + D3 + D4 / 1 + D + D2 + D3 + D4",
			"G2/G3 = 1 + D2 + D4     / 1 + D + D2 + D3 + D4",
			"G3/G3 = 1",
		]
	),

	# TCH_AFS_7_95 definition
	ConvolutionalCode(
		165,
		[
			(  1,  1 ),
			( G5, G4 ),
			( G6, G4 ),
		],
		puncture = [
			  1,   2,   4,   5,   8,  22,  70, 118, 166, 214, 262, 310,
			317, 319, 325, 332, 334, 341, 343, 349, 356, 358, 365, 367,
			373, 380, 382, 385, 389, 391, 397, 404, 406, 409, 413, 415,
			421, 428, 430, 433, 437, 439, 445, 452, 454, 457, 461, 463,
			469, 476, 478, 481, 485, 487, 490, 493, 500, 502, 503, 505,
			506, 508, 509, 511, 512, -1
		],
		name = 'tch_afs_7_95',
		description = [
			"TCH/AFS 7.95 kbits convolutional code:",
			"G4/G4 = 1",
			"G5/G4 = 1 + D + D4 + D6           / 1 + D2 + D3 + D5 + D6",
			"G6/G4 = 1 + D + D2 + D3 + D4 + D6 / 1 + D2 + D3 + D5 + D6",
		]
	),

	# TCH_AFS_7_4 definition
	ConvolutionalCode(
		154,
		[
			( G1, G3 ),
			( G2, G3 ),
			(  1,  1 ),
		],
		puncture = [
			  0, 355, 361, 367, 373, 379, 385, 391, 397, 403, 409, 415,
			421, 427, 433, 439, 445, 451, 457, 460, 463, 466, 468, 469,
			471, 472, -1
		],
		name = 'tch_afs_7_4',
		description = [
			"TCH/AFS 7.4 kbits convolutional code:",
			"G1/G3 = 1 + D + D3 + D4 / 1 + D + D2 + D3 + D4",
			"G2/G3 = 1 + D2 + D4     / 1 + D + D2 + D3 + D4",
			"G3/G3 = 1",
		]
	),

	# TCH_AFS_6_7 definition
	ConvolutionalCode(
		140,
		[
			( G1, G3 ),
			( G2, G3 ),
			(  1,  1 ),
			(  1,  1 ),
		],
		puncture = [
			  1,   3,   7,  11,  15,  27,  39,  55,  67,  79,  95, 107,
			119, 135, 147, 159, 175, 187, 199, 215, 227, 239, 255, 267,
			279, 287, 291, 295, 299, 303, 307, 311, 315, 319, 323, 327,
			331, 335, 339, 343, 347, 351, 355, 359, 363, 367, 369, 371,
			375, 377, 379, 383, 385, 387, 391, 393, 395, 399, 401, 403,
			407, 409, 411, 415, 417, 419, 423, 425, 427, 431, 433, 435,
			439, 441, 443, 447, 449, 451, 455, 457, 459, 463, 465, 467,
			471, 473, 475, 479, 481, 483, 487, 489, 491, 495, 497, 499,
			503, 505, 507, 511, 513, 515, 519, 521, 523, 527, 529, 531,
			535, 537, 539, 543, 545, 547, 549, 551, 553, 555, 557, 559,
			561, 563, 565, 567, 569, 571, 573, 575, -1
		],
		name = 'tch_afs_6_7',
		description = [
			"TCH/AFS 6.7 kbits convolutional code:",
			"G1/G3 = 1 + D + D3 + D4 / 1 + D + D2 + D3 + D4",
			"G2/G3 = 1 + D2 + D4     / 1 + D + D2 + D3 + D4",
			"G3/G3 = 1",
			"G3/G3 = 1",
		]
	),

	# TCH_AFS_5_9 definition
	ConvolutionalCode(
		124,
		[
			( G4, G6 ),
			( G5, G6 ),
			(  1,  1),
			(  1,  1),
		],
		puncture = [
			  0,   1,   3,   5,   7,  11,  15,  31,  47,  63,  79,  95,
			111, 127, 143, 159, 175, 191, 207, 223, 239, 255, 271, 287,
			303, 319, 327, 331, 335, 343, 347, 351, 359, 363, 367, 375,
			379, 383, 391, 395, 399, 407, 411, 415, 423, 427, 431, 439,
			443, 447, 455, 459, 463, 467, 471, 475, 479, 483, 487, 491,
			495, 499, 503, 507, 509, 511, 512, 513, 515, 516, 517, 519,
			-1
		],
		name = 'tch_afs_5_9',
		description = [
			"TCH/AFS 5.9 kbits convolutional code:",
			"124 bits",
			"G4/G6 = 1 + D2 + D3 + D5 + D6 / 1 + D + D2 + D3 + D4 + D6",
			"G5/G6 = 1 + D + D4 + D6       / 1 + D + D2 + D3 + D4 + D6",
			"G6/G6 = 1",
			"G6/G6 = 1",
		]
	),

	# TCH_AFS_5_15 definition
	ConvolutionalCode(
		109,
		[
			( G1, G3 ),
			( G1, G3 ),
			( G2, G3 ),
			(  1,  1 ),
			(  1,  1 ),
		],
		puncture = [
			  0,   4,   5,   9,  10,  14,  15,  20,  25,  30,  35,  40,
			 50,  60,  70,  80,  90, 100, 110, 120, 130, 140, 150, 160,
			170, 180, 190, 200, 210, 220, 230, 240, 250, 260, 270, 280,
			290, 300, 310, 315, 320, 325, 330, 334, 335, 340, 344, 345,
			350, 354, 355, 360, 364, 365, 370, 374, 375, 380, 384, 385,
			390, 394, 395, 400, 404, 405, 410, 414, 415, 420, 424, 425,
			430, 434, 435, 440, 444, 445, 450, 454, 455, 460, 464, 465,
			470, 474, 475, 480, 484, 485, 490, 494, 495, 500, 504, 505,
			510, 514, 515, 520, 524, 525, 529, 530, 534, 535, 539, 540,
			544, 545, 549, 550, 554, 555, 559, 560, 564, -1
		],
		name = 'tch_afs_5_15',
		description = [
			"TCH/AFS 5.15 kbits convolutional code:",
			"G1/G3 = 1 + D + D3 + D4 / 1 + D + D2 + D3 + D4",
			"G1/G3 = 1 + D + D3 + D4 / 1 + D + D2 + D3 + D4",
			"G2/G3 = 1 + D2 + D4     / 1 + D + D2 + D3 + D4",
			"G3/G3 = 1",
			"G3/G3 = 1",
		]
	),

	# TCH_AFS_4_75 definition
	ConvolutionalCode(
		101,
		[
			( G4, G6 ),
			( G4, G6 ),
			( G5, G6 ),
			(  1,  1 ),
			(  1,  1 ),
		],
		puncture = [
			  0,   1,   2,   4,   5,   7,   9,  15,  25,  35,  45,  55,
			 65,  75,  85,  95, 105, 115, 125, 135, 145, 155, 165, 175,
			185, 195, 205, 215, 225, 235, 245, 255, 265, 275, 285, 295,
			305, 315, 325, 335, 345, 355, 365, 375, 385, 395, 400, 405,
			410, 415, 420, 425, 430, 435, 440, 445, 450, 455, 459, 460,
			465, 470, 475, 479, 480, 485, 490, 495, 499, 500, 505, 509,
			510, 515, 517, 519, 520, 522, 524, 525, 526, 527, 529, 530,
			531, 532, 534, -1
		],
		name = 'tch_afs_4_75',
		description = [
			"TCH/AFS 4.75 kbits convolutional code:",
			"G4/G6 = 1 + D2 + D3 + D5 + D6 / 1 + D + D2 + D3 + D4 + D6",
			"G4/G6 = 1 + D2 + D3 + D5 + D6 / 1 + D + D2 + D3 + D4 + D6",
			"G5/G6 = 1 + D + D4 + D6       / 1 + D + D2 + D3 + D4 + D6",
			"G6/G6 = 1",
			"G6/G6 = 1",
		]
	),

	# TCH_FR definition
	ConvolutionalCode(
		185,
		shared_polys["xcch"],
		name = "tch_fr",
		description = ["TCH/F convolutional code"]
	),

	# TCH_HR definition
	ConvolutionalCode(
		98,
		[
			( G4, 1 ),
			( G5, 1 ),
			( G6, 1 ),
		],
		puncture = [
			  1,   4,   7,  10,  13,  16,  19,  22,  25,  28,  31,  34,
			 37,  40,  43,  46,  49,  52,  55,  58,  61,  64,  67,  70,
			 73,  76,  79,  82,  85,  88,  91,  94,  97, 100, 103, 106,
			109, 112, 115, 118, 121, 124, 127, 130, 133, 136, 139, 142,
			145, 148, 151, 154, 157, 160, 163, 166, 169, 172, 175, 178,
			181, 184, 187, 190, 193, 196, 199, 202, 205, 208, 211, 214,
			217, 220, 223, 226, 229, 232, 235, 238, 241, 244, 247, 250,
			253, 256, 259, 262, 265, 268, 271, 274, 277, 280, 283, 295,
			298, 301, 304, 307, 310, -1,
		],
		name = "tch_hr",
		description = ["TCH/H convolutional code"]
	),

	# TCH_AHS_7_95 definition
	ConvolutionalCode(
		129,
		[
			(  1,  1 ),
			( G1, G0 ),
		],
		puncture = [
			  1,   3,   5,   7,  11,  15,  19,  23,  27,  31,  35,  43,
			 47,  51,  55,  59,  63,  67,  71,  79,  83,  87,  91,  95,
			 99, 103, 107, 115, 119, 123, 127, 131, 135, 139, 143, 151,
			155, 159, 163, 167, 171, 175, 177, 179, 183, 185, 187, 191,
			193, 195, 197, 199, 203, 205, 207, 211, 213, 215, 219, 221,
			223, 227, 229, 231, 233, 235, 239, 241, 243, 247, 249, 251,
			255, 257, 259, 261, 263, 265, -1,
		],
		name = "tch_ahs_7_95",
		description = ["TCH/AHS 7.95 kbits convolutional code"]
	),

	# TCH_AHS_7_4 definition
	ConvolutionalCode(
		126,
		[
			(  1,  1 ),
			( G1, G0 ),
		],
		puncture = [
			  1,   3,   7,  11,  19,  23,  27,  35,  39,  43,  51,  55,
			 59,  67,  71,  75,  83,  87,  91,  99, 103, 107, 115, 119,
			123, 131, 135, 139, 143, 147, 151, 155, 159, 163, 167, 171,
			175, 179, 183, 187, 191, 195, 199, 203, 207, 211, 215, 219,
			221, 223, 227, 229, 231, 235, 237, 239, 243, 245, 247, 251,
			253, 255, 257, 259, -1,
		],
		name = "tch_ahs_7_4",
		description = ["TCH/AHS 7.4 kbits convolutional code"]
	),

	# TCH_AHS_6_7 definition
	ConvolutionalCode(
		116,
		[
			(  1,  1 ),
			( G1, G0 ),
		],
		puncture = [
			  1,   3,   9,  19,  29,  39,  49,  59,  69,  79,  89,  99,
			109, 119, 129, 139, 149, 159, 167, 169, 177, 179, 187, 189,
			197, 199, 203, 207, 209, 213, 217, 219, 223, 227, 229, 231,
			233, 235, 237, 239, -1,
		],
		name = "tch_ahs_6_7",
		description = ["TCH/AHS 6.7 kbits convolutional code"]
	),

	# TCH_AHS_5_9 definition
	ConvolutionalCode(
		108,
		[
			(  1,  1 ),
			( G1, G0 ),
		],
		puncture = [
			  1,  15,  71, 127, 139, 151, 163, 175, 187, 195, 203, 211,
			215, 219, 221, 223, -1,
		],
		name = "tch_ahs_5_9",
		description = ["TCH/AHS 5.9 kbits convolutional code"]
	),

	# TCH_AHS_5_15 definition
	ConvolutionalCode(
		97,
		[
			( G1, G3 ),
			( G2, G3 ),
			(  1,  1 ),
		],
		puncture = [
			  0,   1,   3,   4,   6,   9,  12,  15,  18,  21,  27,  33,
			 39,  45,  51,  54, 57,  63,  69,  75,  81,  87,  90,  93,
			 99, 105, 111, 117, 123, 126, 129, 135, 141, 147, 153, 159,
			162, 165, 168, 171, 174, 177, 180, 183, 186, 189, 192, 195,
			198, 201, 204, 207, 210, 213, 216, 219, 222, 225, 228, 231,
			234, 237, 240, 243, 244, 246, 249, 252, 255, 256, 258, 261,
			264, 267, 268, 270, 273, 276, 279, 280, 282, 285, 288, 289,
			291, 294, 295, 297, 298, 300, 301, -1,
		],
		name = "tch_ahs_5_15",
		description = ["TCH/AHS 5.15 kbits convolutional code"]
	),

	# TCH_AHS_4_75 definition
	ConvolutionalCode(
		89,
		[
			(  1,  1 ),
			( G5, G4 ),
			( G6, G4 ),
		],
		puncture = [
			  1,   2,   4,   5,   7,   8,  10,  13,  16,  22,  28,  34,
			 40,  46,  52,  58, 64,  70,  76,  82,  88,  94, 100, 106,
			112, 118, 124, 130, 136, 142, 148, 151, 154, 160, 163, 166,
			172, 175, 178, 184, 187, 190, 196, 199, 202, 208, 211, 214,
			220, 223, 226, 232, 235, 238, 241, 244, 247, 250, 253, 256,
			259, 262, 265, 268, 271, 274, 275, 277, 278, 280, 281, 283,
			284, -1,
		],
		name = "tch_ahs_4_75",
		description = ["TCH/AHS 4.75 kbits convolutional code"]
	),

	# EDGE MCS1_DL_HDR definition
	ConvolutionalCode(
		36,
		shared_polys["mcs"],
		name = "mcs1_dl_hdr",
		term_type = "CONV_TERM_TAIL_BITING",
		description = [
			"EDGE MCS-1 DL header convolutional code:",
			"42 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS1_UL_HDR definition
	ConvolutionalCode(
		39,
		shared_polys["mcs"],
		name = "mcs1_ul_hdr",
		term_type = "CONV_TERM_TAIL_BITING",
		description = [
			"EDGE MCS-1 UL header convolutional code:",
			"45 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS1 definition
	ConvolutionalCode(
		190,
		shared_polys["mcs"],
		name = "mcs1",
		description = [
			"EDGE MCS-1 data convolutional code:",
			"196 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS2 definition
	ConvolutionalCode(
		238,
		shared_polys["mcs"],
		name = "mcs2",
		description = [
			"EDGE MCS-2 data convolutional code:",
			"244 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS3 definition
	ConvolutionalCode(
		310,
		shared_polys["mcs"],
		name = "mcs3",
		description = [
			"EDGE MCS-3 data convolutional code:",
			"316 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS4 definition
	ConvolutionalCode(
		366,
		shared_polys["mcs"],
		name = "mcs4",
		description = [
			"EDGE MCS-4 data convolutional code:",
			"372 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS5_DL_HDR definition
	ConvolutionalCode(
		33,
		shared_polys["mcs"],
		name = "mcs5_dl_hdr",
		term_type = "CONV_TERM_TAIL_BITING",
		description = [
			"EDGE MCS-5 DL header convolutional code:",
			"39 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS5_UL_HDR definition
	ConvolutionalCode(
		45,
		shared_polys["mcs"],
		name = "mcs5_ul_hdr",
		term_type = "CONV_TERM_TAIL_BITING",
		description = [
			"EDGE MCS-5 UL header convolutional code:",
			"51 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS5 definition
	ConvolutionalCode(
		462,
		shared_polys["mcs"],
		name = "mcs5",
		description = [
			"EDGE MCS-5 data convolutional code:",
			"468 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS6 definition
	ConvolutionalCode(
		606,
		shared_polys["mcs"],
		name = "mcs6",
		description = [
			"EDGE MCS-6 data convolutional code:",
			"612 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS7_DL_HDR definition
	ConvolutionalCode(
		45,
		shared_polys["mcs"],
		name = "mcs7_dl_hdr",
		term_type = "CONV_TERM_TAIL_BITING",
		description = [
			"EDGE MCS-7 DL header convolutional code:",
			"51 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS7_UL_HDR definition
	ConvolutionalCode(
		54,
		shared_polys["mcs"],
		name = "mcs7_ul_hdr",
		term_type = "CONV_TERM_TAIL_BITING",
		description = [
			"EDGE MCS-7 UL header convolutional code:",
			"60 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS7 definition
	ConvolutionalCode(
		462,
		shared_polys["mcs"],
		name = "mcs7",
		description = [
			"EDGE MCS-7 data convolutional code:",
			"468 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS8 definition
	ConvolutionalCode(
		558,
		shared_polys["mcs"],
		name = "mcs8",
		description = [
			"EDGE MCS-8 data convolutional code:",
			"564 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),

	# EDGE MCS9 definition
	ConvolutionalCode(
		606,
		shared_polys["mcs"],
		name = "mcs9",
		description = [
			"EDGE MCS-9 data convolutional code:",
			"612 bits blocks, rate 1/3, k = 7",
			"G4 = 1 + D2 + D3 + D5 + D6",
			"G7 = 1 + D + D2 + D3 + D6",
			"G5 = 1 + D + D4 + D6"
		]
	),
]
