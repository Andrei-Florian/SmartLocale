# Overview
In the age of IoT, businesses are digitalising and moving from pen and paper to screen and keyboard. Many managers are appealing to IoT solutions to better run their companies. 

IoT helps businesses improve their sales and marketing by collecting data about their consumers and suggesting actions that would make the business more attractive, resulting in improved sales and presence at the store.
The problem is that IoT is expensive and difficult to implement, and although major businesses will have such solutions implemented, your local café, restaurant and store may not have them! IoT should be accessible for all companies big and small.

SmartLocale aims to bring your business to the digital world in minutes, removing the cost and expertise required to set up a system that tracks and predicts sales and presence in the store, café, restaurant or any other small business.

SmartLocale is an application that tracks the number of people that entered the locale and the number of sales carried out every business day. An edge-device is placed at the store’s POS collecting data about the sales and at the entrance of the store counting the number of people entering and leaving the locale. These two values are compared resulting in crucial business indicators. 

All the data is centralised in the cloud where it is forecasted to predict the business’ sales and presence for the next period of time allowing the business to prepare in the case of a rainy day. 

The manager can access an online, interactive dashboard presenting the historical and forecasted sales and presence in the store. This allows the manager to easily manage their business from their phone, tablet or laptop at any time.

The application also allows the business to see which products were sold the most. The application groups all products into three categories defined by the manager and tracks the sale of products from each category.

# Benefits
The benefits brought by using SmartLocale to the business are summarised below.

1. Visualise the number of products sold from each product category allowing the manager to see which category is most successful.
2. Illustrates the past sales and presence so that the manager can see how the business is doing over time.
3. Forecasts presence and sales in the future allowing the business to prepare for a time with less sales.
4. Easy to set up and use and cost-efficient

# Project Architecture
1.	The frontend device will check if the beam from the break beam sensor is broken and if a button is pressed. If so, the device will increment the respective variables.
2.	The device will continue running this code until it is the end of the business day. When this happens, the device will get the time from its RTC, process the variables and then send the data to the cloud.
3.	The data is received by Azure IoT and is then extracted by a Stream Analytics Job inputting the data into a Power Bi dataset.
4.	The data is finally extracted by a Power Bi Report which displays the data on graphs and other visuals.
