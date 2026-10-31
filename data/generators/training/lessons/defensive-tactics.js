// SPDX-License-Identifier: GPL-3.0-or-later
import {colony,encounter} from './common.js';
export function setup(c) {
    colony(c); encounter(c,2);
    for(let i=0;i<4;i++) c.placeUnit(0,2,1,90+i,98);
}
