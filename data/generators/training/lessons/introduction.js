// SPDX-License-Identifier: GPL-3.0-or-later
import {colony} from './common.js';
export function setup(c) {
    colony(c,{intro:true});
    for(let i=0;i<4;i++) c.placeUnit(0,2,0,82+i,102);
}
