// SPDX-License-Identifier: MIT
import { mount } from "svelte";
import App from "./App.svelte";
import "../../common.css";

mount(App, { target: document.getElementById("app")! });
